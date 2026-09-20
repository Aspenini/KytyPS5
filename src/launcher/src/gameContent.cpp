#include "gameContent.h"

#include "common/file.h"
#include "common/stringUtils.h"
#include "common/zarchive.h"

#include <QFileInfo>

#include <limits>

namespace GameContent {

std::filesystem::path ToPath(const QString& path) {
#if defined(_WIN32)
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(path.toStdString());
#endif
}

QString FromPath(const std::filesystem::path& path) {
#if defined(_WIN32)
	return QString::fromStdWString(path.wstring());
#else
	return QString::fromStdString(Common::PathToString(path));
#endif
}

bool IsArchive(const QString& base) {
	const QFileInfo info(base);
	return info.isFile() && info.suffix().compare(QStringLiteral("zar"), Qt::CaseInsensitive) == 0;
}

std::filesystem::path Resolve(const QString& base, const QString& relative) {
	const auto root = ToPath(base);
	return IsArchive(base) ? Common::MakeZArchivePath(root, ToPath(relative))
	                       : root / ToPath(relative);
}

bool FileExists(const QString& base, const QString& relative) {
	return Common::File::IsFileExisting(Resolve(base, relative));
}

QByteArray ReadFile(const QString& base, const QString& relative) {
	return ReadPath(Resolve(base, relative));
}

QByteArray ReadPath(const std::filesystem::path& path) {
	Common::File file(path, Common::File::Mode::Read);
	if (file.IsInvalid() || file.Size() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
		return {};
	}

	const auto data = file.ReadWholeBuffer();
	return QByteArray(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
}

QStringList ListFiles(const QString& base, const QString& relative) {
	const auto  directory = Resolve(base, relative);
	QStringList result;
	for (const auto& entry: Common::File::GetDirEntries(directory)) {
		if (entry.is_file) {
			result.append(FromPath(directory / std::filesystem::path(entry.name)));
		}
	}
	return result;
}

} // namespace GameContent
