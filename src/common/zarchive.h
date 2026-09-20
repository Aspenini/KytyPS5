#ifndef KYTY_COMMON_ZARCHIVE_H_
#define KYTY_COMMON_ZARCHIVE_H_

#include "common/common.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Common {

struct ZArchiveDirEntry {
	std::string name;
	bool        is_file;
};

class ZArchiveFile {
public:
	~ZArchiveFile();

	[[nodiscard]] uint64_t Size() const;
	[[nodiscard]] uint64_t Tell() const;
	bool                   Seek(uint64_t offset);
	void                   Read(void* data, uint32_t size, uint32_t* bytes_read);
	[[nodiscard]] const std::filesystem::path& ArchivePath() const;

	KYTY_CLASS_NO_COPY(ZArchiveFile);

private:
	struct Private;
	explicit ZArchiveFile(std::unique_ptr<Private> p);

	std::unique_ptr<Private> m_p;

	friend std::unique_ptr<ZArchiveFile> OpenZArchiveFile(const std::filesystem::path& path);
};

// Virtual paths use "archive.zar!/member". The marker keeps archive members
// distinct from host paths while still allowing std::filesystem path joins.
[[nodiscard]] std::filesystem::path MakeZArchivePath(const std::filesystem::path& archive,
                                                     const std::filesystem::path& member = {});
[[nodiscard]] bool                  IsZArchivePath(const std::filesystem::path& path);
[[nodiscard]] bool                  IsZArchiveFile(const std::filesystem::path& path);
[[nodiscard]] bool                  IsZArchiveDirectory(const std::filesystem::path& path);
[[nodiscard]] uint64_t              ZArchiveFileSize(const std::filesystem::path& path);
[[nodiscard]] std::vector<ZArchiveDirEntry>
GetZArchiveDirEntries(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path         GetZArchiveHostPath(const std::filesystem::path& path);
[[nodiscard]] std::unique_ptr<ZArchiveFile> OpenZArchiveFile(const std::filesystem::path& path);

} // namespace Common

#endif /* KYTY_COMMON_ZARCHIVE_H_ */
