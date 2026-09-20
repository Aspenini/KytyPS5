#include "common/zarchive.h"

#include "common/stringUtils.h"

#include <algorithm>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <zarchive/zarchivereader.h>

namespace Common {

namespace {

struct ParsedPath {
	std::filesystem::path archive;
	std::string           member;
};

struct SharedReader {
	explicit SharedReader(ZArchiveReader* value): reader(value) {}
	~SharedReader() { delete reader; }

	KYTY_CLASS_NO_COPY(SharedReader);

	ZArchiveReader* reader;
	std::mutex      mutex;
};

std::mutex                                                     g_readers_mutex;
std::unordered_map<std::string, std::shared_ptr<SharedReader>> g_readers;

bool ParsePath(const std::filesystem::path& path, ParsedPath* parsed) {
	auto       text      = path.native();
	const auto slash     = std::filesystem::path("/").native().front();
	const auto backslash = std::filesystem::path("\\").native().front();
	std::replace(text.begin(), text.end(), backslash, slash);
	auto lower = text;
	for (auto& c: lower) {
		const auto upper_a = static_cast<std::filesystem::path::value_type>('A');
		const auto upper_z = static_cast<std::filesystem::path::value_type>('Z');
		if (c >= upper_a && c <= upper_z) {
			c += static_cast<std::filesystem::path::value_type>('a' - 'A');
		}
	}
	const auto marker_text = std::filesystem::path(".zar!").native();
	auto       marker      = lower.find(marker_text);
	if (marker == std::filesystem::path::string_type::npos) {
		return false;
	}
	auto mark = marker + 4;
	if ((mark + 1 < text.size() && text[mark + 1] != slash)) {
		return false;
	}

	auto member_native =
	    mark + 1 < text.size() ? text.substr(mark + 1) : std::filesystem::path::string_type {};
	std::string member = Common::PathToGenericString(std::filesystem::path(member_native));
	while (member.starts_with('/')) {
		member.erase(0, 1);
	}

	std::string normalized;
	for (const auto& component: Common::Split(member, "/")) {
		if (component.empty() || component == ".") {
			continue;
		}
		if (component == "..") {
			return false;
		}
		if (!normalized.empty()) {
			normalized += '/';
		}
		normalized += component;
	}

	if (parsed != nullptr) {
		parsed->archive = std::filesystem::path(text.substr(0, mark));
		parsed->member  = std::move(normalized);
	}
	return true;
}

std::shared_ptr<SharedReader> GetReader(const std::filesystem::path& archive) {
	auto key = Common::PathToGenericString(archive);
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	key = Common::ToLower(key);
#endif

	std::lock_guard lock(g_readers_mutex);
	if (auto it = g_readers.find(key); it != g_readers.end()) {
		return it->second;
	}

	auto* reader = ZArchiveReader::OpenFromFile(archive);
	if (reader == nullptr) {
		return {};
	}

	auto shared    = std::make_shared<SharedReader>(reader);
	g_readers[key] = shared;
	return shared;
}

ZArchiveNodeHandle Lookup(const std::shared_ptr<SharedReader>& reader, std::string_view member) {
	if (reader == nullptr || reader->reader == nullptr) {
		return ZARCHIVE_INVALID_NODE;
	}
	std::lock_guard lock(reader->mutex);
	return reader->reader->LookUp(member);
}

} // namespace

struct ZArchiveFile::Private {
	std::shared_ptr<SharedReader> reader;
	std::filesystem::path         archive;
	ZArchiveNodeHandle            node     = ZARCHIVE_INVALID_NODE;
	uint64_t                      size     = 0;
	uint64_t                      position = 0;
};

ZArchiveFile::ZArchiveFile(std::unique_ptr<Private> p): m_p(std::move(p)) {}

ZArchiveFile::~ZArchiveFile() = default;

uint64_t ZArchiveFile::Size() const {
	return m_p->size;
}

uint64_t ZArchiveFile::Tell() const {
	return m_p->position;
}

bool ZArchiveFile::Seek(uint64_t offset) {
	m_p->position = offset;
	return true;
}

void ZArchiveFile::Read(void* data, uint32_t size, uint32_t* bytes_read) {
	uint64_t read = 0;
	if (data != nullptr && size != 0 && m_p->position < m_p->size) {
		const auto      requested = std::min<uint64_t>(size, m_p->size - m_p->position);
		std::lock_guard lock(m_p->reader->mutex);
		read = m_p->reader->reader->ReadFromFile(m_p->node, m_p->position, requested, data);
		m_p->position += read;
	}
	if (bytes_read != nullptr) {
		*bytes_read = static_cast<uint32_t>(read);
	}
}

const std::filesystem::path& ZArchiveFile::ArchivePath() const {
	return m_p->archive;
}

std::filesystem::path MakeZArchivePath(const std::filesystem::path& archive,
                                       const std::filesystem::path& member) {
	auto       text      = archive.native();
	const auto slash     = std::filesystem::path("/").native().front();
	const auto backslash = std::filesystem::path("\\").native().front();
	std::replace(text.begin(), text.end(), backslash, slash);
	text += std::filesystem::path("!").native();
	auto rel = member.native();
	std::replace(rel.begin(), rel.end(), backslash, slash);
	while (rel.starts_with(slash)) {
		rel.erase(0, 1);
	}
	if (!rel.empty()) {
		text += slash;
		text += rel;
	}
	return std::filesystem::path(text);
}

bool IsZArchivePath(const std::filesystem::path& path) {
	return ParsePath(path, nullptr);
}

bool IsZArchiveFile(const std::filesystem::path& path) {
	ParsedPath parsed;
	if (!ParsePath(path, &parsed)) {
		return false;
	}
	auto reader = GetReader(parsed.archive);
	auto node   = Lookup(reader, parsed.member);
	if (node == ZARCHIVE_INVALID_NODE) {
		return false;
	}
	std::lock_guard lock(reader->mutex);
	return reader->reader->IsFile(node);
}

bool IsZArchiveDirectory(const std::filesystem::path& path) {
	ParsedPath parsed;
	if (!ParsePath(path, &parsed)) {
		return false;
	}
	auto reader = GetReader(parsed.archive);
	auto node   = Lookup(reader, parsed.member);
	if (node == ZARCHIVE_INVALID_NODE) {
		return false;
	}
	std::lock_guard lock(reader->mutex);
	return reader->reader->IsDirectory(node);
}

uint64_t ZArchiveFileSize(const std::filesystem::path& path) {
	ParsedPath parsed;
	if (!ParsePath(path, &parsed)) {
		return 0;
	}
	auto reader = GetReader(parsed.archive);
	auto node   = Lookup(reader, parsed.member);
	if (node == ZARCHIVE_INVALID_NODE) {
		return 0;
	}
	std::lock_guard lock(reader->mutex);
	return reader->reader->IsFile(node) ? reader->reader->GetFileSize(node) : 0;
}

std::vector<ZArchiveDirEntry> GetZArchiveDirEntries(const std::filesystem::path& path) {
	ParsedPath parsed;
	if (!ParsePath(path, &parsed)) {
		return {};
	}
	auto reader = GetReader(parsed.archive);
	auto node   = Lookup(reader, parsed.member);
	if (node == ZARCHIVE_INVALID_NODE) {
		return {};
	}

	std::lock_guard lock(reader->mutex);
	if (!reader->reader->IsDirectory(node)) {
		return {};
	}

	const auto                    count = reader->reader->GetDirEntryCount(node);
	std::vector<ZArchiveDirEntry> result;
	result.reserve(count);
	for (uint32_t index = 0; index < count; index++) {
		ZArchiveReader::DirEntry entry {};
		if (reader->reader->GetDirEntry(node, index, entry)) {
			result.push_back({std::string(entry.name), entry.isFile});
		}
	}
	return result;
}

std::filesystem::path GetZArchiveHostPath(const std::filesystem::path& path) {
	ParsedPath parsed;
	return ParsePath(path, &parsed) ? parsed.archive : std::filesystem::path {};
}

std::unique_ptr<ZArchiveFile> OpenZArchiveFile(const std::filesystem::path& path) {
	ParsedPath parsed;
	if (!ParsePath(path, &parsed)) {
		return {};
	}
	auto reader = GetReader(parsed.archive);
	auto node   = Lookup(reader, parsed.member);
	if (node == ZARCHIVE_INVALID_NODE) {
		return {};
	}

	uint64_t size = 0;
	{
		std::lock_guard lock(reader->mutex);
		if (!reader->reader->IsFile(node)) {
			return {};
		}
		size = reader->reader->GetFileSize(node);
	}

	auto p     = std::make_unique<ZArchiveFile::Private>();
	p->reader  = std::move(reader);
	p->archive = std::move(parsed.archive);
	p->node    = node;
	p->size    = size;
	return std::unique_ptr<ZArchiveFile>(new ZArchiveFile(std::move(p)));
}

} // namespace Common
