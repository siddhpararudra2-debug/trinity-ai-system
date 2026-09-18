// Trinity — Projects are first-class objects.
//
//   My Drone
//   ├── CAD  ├── PCB  ├── Calculations  ├── Simulation
//   ├── Firmware  ├── Research  ├── Artifacts  └── Logs
//
// Metadata lives in SQLite; the workspace folder (inside the user's
// configurable projects root) holds the on-disk material.
#pragma once

#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "../db/Database.hpp"

namespace trinity::projects {

struct Project {
    std::string project_id;
    std::string name;
    std::string description;
    std::string workspace;      // absolute path of the project folder
    bool archived = false;
    std::string created_at;     // ISO-8601 UTC
    std::string updated_at;

    core::Json to_json() const;
    static Project from_json(const core::Json& json);
};

// Sub-folders every project gets on creation (brief §Project System).
extern const char* const kStandardFolders[];
extern const int kStandardFolderCount;

// Canonical project-store interface (brief §Core interfaces: IProjectStore).
class IProjectStore {
public:
    virtual ~IProjectStore() = default;

    virtual core::Result<Project> create(const std::string& name,
                                         const std::string& description = "",
                                         const std::string& explicit_workspace = "") = 0;
    virtual core::Result<Project> get(const std::string& project_id) const = 0;
    virtual core::Result<std::vector<Project>> list(bool include_archived = false) const = 0;
    virtual core::Result<Project> rename(const std::string& project_id,
                                         const std::string& new_name) = 0;
    virtual core::Result<Project> set_description(const std::string& project_id,
                                                  const std::string& description) = 0;
    virtual core::Result<Project> archive(const std::string& project_id, bool archived) = 0;
    virtual core::Result<Project> duplicate(const std::string& project_id,
                                            const std::string& new_name) = 0;
    virtual core::Result<Project> import_from_folder(const std::string& source_folder,
                                                     const std::string& new_name) = 0;
    virtual core::Status export_to_folder(const std::string& project_id,
                                          const std::string& destination_folder) = 0;
};

class ProjectStore : public IProjectStore {
public:
    explicit ProjectStore(db::Database& db, std::string projects_root);

    // Creates the metadata row and the on-disk folder structure.
    core::Result<Project> create(const std::string& name, const std::string& description = "",
                                 const std::string& explicit_workspace = "") override;

    core::Result<Project> get(const std::string& project_id) const override;
    core::Result<std::vector<Project>> list(bool include_archived = false) const override;

    core::Result<Project> rename(const std::string& project_id, const std::string& new_name) override;
    core::Result<Project> set_description(const std::string& project_id,
                                          const std::string& description) override;
    core::Result<Project> archive(const std::string& project_id, bool archived) override;
    core::Result<Project> duplicate(const std::string& project_id,
                                    const std::string& new_name) override;

    // Export = copy the project folder into a .zip-less folder bundle next to
    // the destination; Import = copy it back in and register metadata.
    // (Archive format comes with the packaging phase; both are explicit
    // folder copies now, no fabrication of zip support.)
    core::Result<Project> import_from_folder(const std::string& source_folder,
                                             const std::string& new_name) override;
    core::Status export_to_folder(const std::string& project_id,
                                  const std::string& destination_folder) override;

private:
    core::Result<Project> row_to_project(const core::JsonObject& row) const;
    std::string workspace_for(const std::string& name) const;

    db::Database* db_;
    std::string projects_root_;
};

}  // namespace trinity::projects
