#include "docset.h"

#include <QDir>
#include <QMetaEnum>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using namespace Zeal;

Docset::Docset(const QString &path) :
    m_path(path)
{
    QDir dir(m_path);
    if (!dir.exists())
        return;

    /// TODO: Use metadata
    m_name = dir.dirName().replace(QStringLiteral(".docset"), QString());

    /// TODO: Report errors here and below
    if (!dir.cd(QStringLiteral("Contents")))
        return;

    if (dir.exists(QStringLiteral("Info.plist")))
        info = DocsetInfo::fromPlist(dir.absoluteFilePath(QStringLiteral("Info.plist")));
    else if (dir.exists(QStringLiteral("info.plist")))
        info = DocsetInfo::fromPlist(dir.absoluteFilePath(QStringLiteral("info.plist")));
    else
        return;

    // Read metadata
    metadata = DocsetMetadata::fromFile(path + QStringLiteral("/meta.json"));

    if (info.family == QStringLiteral("cheatsheet"))
        m_name = QString(QStringLiteral("%1_cheats")).arg(m_name);

    if (!dir.cd(QStringLiteral("Resources")))
        return;

    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
    db.setDatabaseName(dir.absoluteFilePath(QStringLiteral("docSet.dsidx")));

    if (!db.open())
        return;

    QSqlQuery query = db.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"));

    if (query.lastError().type() != QSqlError::NoError) {
        qWarning("SQL Error: %s", qPrintable(query.lastError().text()));
        return;
    }

    m_type = Docset::Type::ZDash;
    while (query.next()) {
        if (query.value(0).toString() == QStringLiteral("searchIndex")) {
            m_type = Docset::Type::Dash;
            break;
        }
    }

    if (!dir.cd(QStringLiteral("Documents")))
        return;

    prefix = info.bundleName.isEmpty() ? m_name : info.bundleName;

    findIcon();
    countSymbols();

    m_isValid = true;
}

Docset::~Docset()
{
    db.close();
}

bool Docset::isValid() const
{
    return m_isValid;
}

QString Docset::name() const
{
    return m_name;
}

Docset::Type Docset::type() const
{
    return m_type;
}

QString Docset::path() const
{
    return m_path;
}

QString Docset::documentPath() const
{
    return QDir(m_path).absoluteFilePath(QStringLiteral("Contents/Resources/Documents"));
}

QIcon Docset::icon() const
{
    return m_icon;
}

QMap<Docset::SymbolType, int> Docset::symbolCounts() const
{
    return m_symbolCounts;
}

int Docset::symbolCount(Docset::SymbolType type) const
{
    return m_symbolCounts.value(type);
}

int Docset::symbolCount(const QString &typeStr) const
{
    return m_symbolCounts.value(strToSymbolType(typeStr));
}

const QMap<QString, QString> &Docset::symbols(Docset::SymbolType type) const
{
    if (!m_symbols.contains(type))
        loadSymbols(type);
    return m_symbols[type];
}

/// TODO: Remove after refactoring in ListModel
QString Docset::symbolTypeToStr(SymbolType symbolType)
{
    QMetaEnum types = staticMetaObject.enumerator(staticMetaObject.indexOfEnumerator("SymbolType"));
    return types.valueToKey(static_cast<int>(symbolType));
}

/// TODO: Make private
Docset::SymbolType Docset::strToSymbolType(const QString &str)
{
    const static QHash<QString, SymbolType> typeStrings = {
        {QStringLiteral("attribute"), SymbolType::Attribute},
        {QStringLiteral("cl"), SymbolType::Class},
        {QStringLiteral("class"), SymbolType::Class},
        {QStringLiteral("command"), SymbolType::Command},
        {QStringLiteral("clconst"), SymbolType::Constant},
        {QStringLiteral("constant"), SymbolType::Constant},
        {QStringLiteral("constructor"), SymbolType::Constructor},
        {QStringLiteral("conversion"), SymbolType::Conversion},
        {QStringLiteral("delegate"), SymbolType::Delegate},
        {QStringLiteral("directive"), SymbolType::Directive},
        {QStringLiteral("enum"), SymbolType::Enumeration},
        {QStringLiteral("enumeration"), SymbolType::Enumeration},
        {QStringLiteral("event"), SymbolType::Event},
        {QStringLiteral("exception"), SymbolType::Exception},
        {QStringLiteral("field"), SymbolType::Field},
        {QStringLiteral("filter"), SymbolType::Filter},
        {QStringLiteral("func"), SymbolType::Function},
        {QStringLiteral("function"), SymbolType::Function},
        {QStringLiteral("guide"), SymbolType::Guide},
        {QStringLiteral("interface"), SymbolType::Interface},
        {QStringLiteral("macro"), SymbolType::Macro},
        {QStringLiteral("clm"), SymbolType::Method},
        {QStringLiteral("method"), SymbolType::Method},
        {QStringLiteral("module"), SymbolType::Module},
        {QStringLiteral("namespace"), SymbolType::Namespace},
        {QStringLiteral("object"), SymbolType::Object},
        {QStringLiteral("operator"), SymbolType::Operator},
        {QStringLiteral("option"), SymbolType::Option},
        {QStringLiteral("package"), SymbolType::Package},
        {QStringLiteral("property"), SymbolType::Property},
        {QStringLiteral("setting"), SymbolType::Setting},
        {QStringLiteral("specialization"), SymbolType::Specialization},
        {QStringLiteral("struct"), SymbolType::Structure},
        {QStringLiteral("structure"), SymbolType::Structure},
        {QStringLiteral("tag"), SymbolType::Tag},
        {QStringLiteral("trait"), SymbolType::Trait},
        {QStringLiteral("tdef"), SymbolType::Type},
        {QStringLiteral("type"), SymbolType::Type},
        {QStringLiteral("variable"), SymbolType::Variable}
    };

    if (!typeStrings.contains(str.toLower()))
        qWarning("Unknown symbol: %s", qPrintable(str));

    return typeStrings.value(str.toLower(), SymbolType::Invalid);
}

void Docset::findIcon()
{
    const QDir dir(m_path);
    for (const QString &iconFile : dir.entryList({QStringLiteral("icon.*")}, QDir::Files)) {
        m_icon = QIcon(dir.absoluteFilePath(iconFile));
        if (!m_icon.availableSizes().isEmpty())
            return;
    }

    QString bundleName = info.bundleName;
    bundleName.replace(QLatin1String(" "), QLatin1String("_"));
    m_icon = QIcon(QString(QStringLiteral("docsetIcon:%1.png")).arg(bundleName));
    if (!m_icon.availableSizes().isEmpty())
        return;

    // Fallback to identifier and docset file name.
    m_icon = QIcon(QString(QStringLiteral("docsetIcon:%1.png")).arg(info.bundleIdentifier));
    if (!m_icon.availableSizes().isEmpty())
        return;

    m_icon = QIcon(QString(QStringLiteral("docsetIcon:%1.png")).arg(m_name));
    if (!m_icon.availableSizes().isEmpty())
        return;
}

void Docset::countSymbols()
{
    QSqlQuery query;
    if (m_type == Docset::Type::Dash) {
        query = db.exec(QStringLiteral("SELECT type, COUNT(*) FROM searchIndex GROUP BY type"));
    } else if (m_type == Docset::Type::ZDash) {
        query = db.exec(QStringLiteral("SELECT ztypename, COUNT(*) FROM ztoken JOIN ztokentype"
                                       " ON ztoken.ztokentype = ztokentype.z_pk GROUP BY ztypename"));
    }

    if (query.lastError().type() != QSqlError::NoError) {
        qWarning("SQL Error: %s", qPrintable(query.lastError().text()));
        return;
    }

    while (query.next()) {
        const QString symbolTypeStr = query.value(0).toString();
        const SymbolType symbolType = strToSymbolType(symbolTypeStr);
        if (symbolType == SymbolType::Invalid)
            continue;

        m_symbolStrings.insert(symbolType, symbolTypeStr);
        m_symbolCounts.insert(symbolType, query.value(1).toInt());
    }
}

/// TODO: Fetch and cache only portions of symbols
void Docset::loadSymbols(SymbolType symbolType) const
{
    QString queryStr;
    switch (m_type) {
    case Docset::Type::Dash:
        queryStr = QString("SELECT name, path FROM searchIndex WHERE type='%1' ORDER BY name ASC")
                .arg(m_symbolStrings[symbolType]);
        break;
    case Docset::Type::ZDash:
        queryStr = QString("SELECT ztokenname AS name, "
                           "CASE WHEN (zanchor IS NULL) THEN zpath "
                           "ELSE (zpath || '#' || zanchor) "
                           "END AS path FROM ztoken "
                           "JOIN ztokenmetainformation ON ztoken.zmetainformation = ztokenmetainformation.z_pk "
                           "JOIN zfilepath ON ztokenmetainformation.zfile = zfilepath.z_pk "
                           "JOIN ztokentype ON ztoken.ztokentype = ztokentype.z_pk WHERE ztypename='%1' "
                           "ORDER BY ztokenname ASC").arg(m_symbolStrings[symbolType]);
        break;
    }

    QSqlQuery query = db.exec(queryStr);

    if (query.lastError().type() != QSqlError::NoError) {
        qWarning("SQL Error: %s", qPrintable(query.lastError().text()));
        return;
    }

    QMap<QString, QString> &symbols = m_symbols[symbolType];
    while (query.next())
        symbols.insertMulti(query.value(0).toString(), QDir(documentPath()).absoluteFilePath(query.value(1).toString()));
}