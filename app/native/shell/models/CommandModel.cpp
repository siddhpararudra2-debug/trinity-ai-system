#include "CommandModel.hpp"

namespace trinity::shell {

CommandModel::CommandModel(commands::CommandRegistry* registry, QObject* parent)
    : QAbstractListModel(parent), registry_(registry) {
    refresh();
}

int CommandModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(entries_.size());
}
QVariant CommandModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size()))
        return {};
    const auto& e = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case CommandIdRole: return e.commandId;
        case TitleRole: return e.title;
        case CategoryRole: return e.category;
        case KeywordsRole: return e.keywords;
        case SearchTextRole: return e.searchText;
        default: return {};
    }
}
QHash<int, QByteArray> CommandModel::roleNames() const {
    return {
        {CommandIdRole, "commandId"},
        {TitleRole, "title"},
        {CategoryRole, "category"},
        {KeywordsRole, "keywords"},
        {SearchTextRole, "searchText"},
    };
}

void CommandModel::refresh() {
    if (!registry_) return;
    auto cat = registry_->catalogue();
    std::vector<Entry> next;
    next.reserve(cat.size());
    for (auto& j : cat) {
        Entry e;
        if (auto* id = j.find("command_id")) e.commandId = QString::fromStdString(id->as_string());
        else if (auto* id2 = j.find("id")) e.commandId = QString::fromStdString(id2->as_string());
        if (auto* t = j.find("title")) e.title = QString::fromStdString(t->as_string());
        if (auto* c = j.find("category")) e.category = QString::fromStdString(c->as_string());
        if (auto* kw = j.find("keywords")) {
            for (auto& k : kw->as_array()) e.keywords << QString::fromStdString(k.as_string());
        }
        e.searchText = e.title + " " + e.category + " " + e.keywords.join(" ") + " " + e.commandId;
        e.searchText = e.searchText.toLower();
        next.push_back(std::move(e));
    }
    // Include deterministic text commands not in registry (engine.run_command verbs)
    // so palette always offers at least these
    auto ensure = [&](const QString& id, const QString& title, const QString& cat, const QStringList& kw) {
        for (auto& ex : next) if (ex.commandId == id) return;
        Entry e; e.commandId=id; e.title=title; e.category=cat; e.keywords=kw;
        e.searchText = (title+" "+cat+" "+kw.join(" ")+" "+id).toLower();
        next.push_back(std::move(e));
    };
    ensure("palette.open", "Open Command Palette", "System", {"palette","command"});
    ensure("project.new", "New Project…", "Workspace", {"project","create"});
    ensure("project.open", "Open Project…", "Workspace", {"project","open"});
    ensure("project.import", "Import Project…", "Workspace", {"import"});
    ensure("project.export", "Export Project…", "Workspace", {"export"});
    ensure("cad.generate", "Generate CAD — quadcopter frame", "CAD", {"cad","generate","quadcopter"});
    ensure("math.evaluate", "Math — Evaluate Expression", "Math", {"math","calculate","evaluate"});
    ensure("artifact.validate", "Validate Artifact", "Validation", {"validate","verify"});
    ensure("view.jobs", "Show Jobs", "View", {"jobs","monitor"});
    ensure("view.artifacts", "Show Artifacts", "View", {"artifacts"});
    ensure("settings.open", "Open Settings…", "System", {"settings"});

    beginResetModel();
    entries_ = std::move(next);
    endResetModel();
    emit countChanged();
}

QVariantMap CommandModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(entries_.size())) return out;
    const auto& e = entries_[static_cast<std::size_t>(row)];
    out["commandId"] = e.commandId;
    out["title"] = e.title;
    out["category"] = e.category;
    out["keywords"] = e.keywords;
    return out;
}

bool CommandModel::execute(const QString& commandId, const QVariantMap& args) {
    if (!registry_) return false;
    // Map known ids to simple handlers; otherwise delegate to registry
    // Provide useful fallback for job-like commands
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const Entry& e){ return e.commandId==commandId; });
    if (it==entries_.end()) {
        emit errorRaised("request_validation_error", "Unknown command "+commandId);
        return false;
    }
    // Try registry execution
    core::Json jArgs = core::Json::object();
    for (auto it2 = args.begin(); it2 != args.end(); ++it2) {
        jArgs[it2.key().toStdString()] = it2.value().toString().toStdString();
    }
    auto res = registry_->execute(commandId.toStdString(), jArgs);
    if (res.is_ok()) {
        emit commandExecuted(commandId, QString::fromStdString(res.value().dump()));
        return true;
    }
    // If not in registry, treat as palette navigation — still emit success for UI routing
    // The QML side will handle navigation (workspace switches etc.)
    if (res.error().code() == core::ErrorCode::EngineNotFoundError ||
        res.error().code() == core::ErrorCode::RequestValidationError) {
        // Not a registry command — let QML handle via commandExecuted for routing
        emit commandExecuted(commandId, "{}");
        return true;
    }
    emit errorRaised(QString::fromStdString(res.error().code_string()),
                     QString::fromStdString(res.error().message()));
    return false;
}

// ---------- filter ----------

CommandFilterModel::CommandFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(CommandModel::SearchTextRole);
}

void CommandFilterModel::setFilterText(const QString& t) {
    if (filterText_==t) return;
    filterText_=t;
    emit filterTextChanged();
    setFilterRegularExpression(QRegularExpression(QRegularExpression::escape(t), QRegularExpression::CaseInsensitiveOption));
    // QSortFilterProxyModel uses filterRegularExpression; override filterAcceptsRow also handles empty
    invalidateFilter();
}

bool CommandFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (filterText_.isEmpty()) return true;
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    QString text = idx.data(CommandModel::SearchTextRole).toString();
    // also check title/category directly
    QString title = idx.data(CommandModel::TitleRole).toString().toLower();
    QString cat = idx.data(CommandModel::CategoryRole).toString().toLower();
    QString q = filterText_.toLower();
    return text.contains(q) || title.contains(q) || cat.contains(q);
}

QVariantMap CommandFilterModel::get(int row) const {
    QModelIndex proxyIdx = index(row,0);
    QModelIndex src = mapToSource(proxyIdx);
    auto* srcModel = qobject_cast<CommandModel*>(sourceModel());
    if (!srcModel) return {};
    return srcModel->get(src.row());
}
bool CommandFilterModel::executeFiltered(int row, const QVariantMap& args) {
    QModelIndex proxyIdx = index(row,0);
    QModelIndex src = mapToSource(proxyIdx);
    auto* srcModel = qobject_cast<CommandModel*>(sourceModel());
    if (!srcModel) return false;
    auto m = srcModel->get(src.row());
    QString cid = m["commandId"].toString();
    return srcModel->execute(cid, args);
}

} // namespace trinity::shell
