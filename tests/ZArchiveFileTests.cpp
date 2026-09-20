#include "common/file.h"
#include "common/zarchive.h"

#include <zarchive/zarchivewriter.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void Check(bool value, const char *text) {
  if (!value) {
    std::fprintf(stderr, "ZArchiveFileTests: failed: %s\n", text);
    std::abort();
  }
}

class TempDirectory {
public:
  TempDirectory() {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = std::filesystem::temp_directory_path() /
             ("kyty_zarchive_test_!_" + std::to_string(unique));
    Check(std::filesystem::create_directories(m_path),
          "create temporary directory");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
  }

  [[nodiscard]] const std::filesystem::path &Path() const { return m_path; }

  KYTY_CLASS_NO_COPY(TempDirectory);

private:
  std::filesystem::path m_path;
};

struct WriterContext {
  std::filesystem::path path;
  std::ofstream output;
  bool failed = false;
};

void NewOutputFile(int32_t /*part_index*/, void *opaque) {
  auto *context = static_cast<WriterContext *>(opaque);
  context->output =
      std::ofstream(context->path, std::ios::binary | std::ios::trunc);
  context->failed = !context->output.is_open();
}

void WriteOutputData(const void *data, size_t length, void *opaque) {
  auto *context = static_cast<WriterContext *>(opaque);
  context->output.write(static_cast<const char *>(data),
                        static_cast<std::streamsize>(length));
  context->failed = context->failed || !context->output.good();
}

void AddFile(ZArchiveWriter &writer, const char *name, const void *data,
             size_t size) {
  Check(writer.StartNewFile(name), "create archive member");
  writer.AppendData(data, size);
}

void CreateArchive(const std::filesystem::path &path,
                   const std::vector<uint8_t> &payload) {
  WriterContext context{.path = path};
  {
    ZArchiveWriter writer(NewOutputFile, WriteOutputData, &context);
    Check(!context.failed, "open archive output");
    Check(writer.MakeDir("sce_sys", true), "create metadata directory");
    Check(writer.MakeDir("assets/subdir", true),
          "create nested asset directory");

    constexpr char Eboot[] = "ELF fixture";
    constexpr char Param[] =
        R"({"titleId":"TEST00001","titleName":"Archive Test"})";
    AddFile(writer, "eboot.bin", Eboot, sizeof(Eboot) - 1);
    AddFile(writer, "sce_sys/param.json", Param, sizeof(Param) - 1);
    AddFile(writer, "assets/subdir/data.bin", payload.data(), payload.size());
    writer.Finalize();
  }
  context.output.close();
  Check(!context.failed, "write archive output");
}

} // namespace

int main() {
  TempDirectory temporary;
  const auto archive_path =
      temporary.Path() / std::filesystem::path(u8"game-テスト.zar");

  std::vector<uint8_t> payload(3 * 64 * 1024 + 37);
  for (size_t index = 0; index < payload.size(); index++) {
    payload[index] = static_cast<uint8_t>((index * 37 + 11) & 0xff);
  }
  CreateArchive(archive_path, payload);

  const auto root = Common::MakeZArchivePath(archive_path);
  Check(Common::IsZArchivePath(root), "recognize virtual archive root");
  Check(Common::GetZArchiveHostPath(root) == archive_path,
        "recover host archive path");
  Check(Common::File::IsDirectoryExisting(root), "find archive root directory");
  Check(Common::File::IsFileExisting(root / "EBOOT.BIN"),
        "lookup is case insensitive");
  Check(!Common::File::IsFileExisting(root / "missing.bin"),
        "reject missing member");
  Check(!Common::IsZArchivePath(root / "../outside.bin"),
        "reject parent traversal");

  const auto entries = Common::File::GetDirEntries(root);
  Check(std::ranges::any_of(entries,
                            [](const auto &entry) {
                              return entry.is_file && entry.name == "eboot.bin";
                            }),
        "enumerate root file");
  Check(std::ranges::any_of(entries,
                            [](const auto &entry) {
                              return !entry.is_file && entry.name == "assets";
                            }),
        "enumerate root directory");

  const auto data_path = root / "assets/subdir/data.bin";
  Check(Common::File::Size(data_path) == payload.size(), "report member size");
  Common::File file(data_path, Common::File::Mode::Read);
  Check(!file.IsInvalid(), "open archive member");
  Check(file.Remaining() == payload.size(), "report initial remaining bytes");
  Check(file.Seek(64 * 1024 - 19), "seek before compression block boundary");
  std::vector<uint8_t> slice(97);
  uint32_t bytes_read = 0;
  file.Read(slice.data(), static_cast<uint32_t>(slice.size()), &bytes_read);
  Check(bytes_read == slice.size(), "read across compression block boundary");
  Check(
      std::equal(slice.begin(), slice.end(), payload.begin() + 64 * 1024 - 19),
      "preserve data across compression block boundary");

  Check(file.Seek(payload.size() - 9), "seek near member end");
  file.Read(slice.data(), static_cast<uint32_t>(slice.size()), &bytes_read);
  Check(bytes_read == 9, "truncate read at member end");
  Check(file.IsEOF(), "report member end of file");
  Check(file.Seek(payload.size() + 100), "allow seek past member end");
  Check(file.Remaining() == 0, "report no bytes remaining past member end");
  file.Read(slice.data(), static_cast<uint32_t>(slice.size()), &bytes_read);
  Check(bytes_read == 0, "read past member end returns no data");

  Common::File metadata(root / "sce_sys/param.json", Common::File::Mode::Read);
  const auto metadata_data = metadata.ReadWholeBuffer();
  const std::string metadata_text(
      reinterpret_cast<const char *>(metadata_data.data()),
      metadata_data.size());
  Check(metadata_text.find("Archive Test") != std::string::npos,
        "read metadata member");

  Common::File writable;
  Check(!writable.Open(data_path, Common::File::Mode::ReadWrite),
        "reject writable member open");
  Check(!Common::File::DeleteFile(data_path), "reject member deletion");
  Check(!Common::File::CreateDirectory(root / "new-dir"),
        "reject member directory creation");

  std::printf("ZArchiveFileTests: all cases passed\n");
  return 0;
}
