#pragma once

// Repository operations for core models. All SQL lives in the storage
// layer; upper layers (jobs, workflows, artifacts) delegate here.

#include <memory>
#include <string>
#include <vector>

#include "../artifacts/Artifact.hpp"
#include "../core/Json.hpp"
#include "../jobs/Job.hpp"
#include "../workflows/Workflow.hpp"
#include "Database.hpp"

namespace trinity::storage {

class JobRepository {
public:
    explicit JobRepository(std::shared_ptr<Database> db);

    void save(const jobs::Job& job);
    jobs::Job get(const std::string& jobId) const;
    std::vector<jobs::Job> listRecent(int limit = 50) const;
    void remove(const std::string& jobId);

private:
    std::shared_ptr<Database> db_;
};

class WorkflowRepository {
public:
    explicit WorkflowRepository(std::shared_ptr<Database> db);

    void saveWorkflow(const workflows::Workflow& workflow);
    workflows::Workflow getWorkflow(const std::string& workflowId) const;
    std::vector<workflows::Workflow> listRecent(int limit = 50) const;
    void saveNode(const std::string& workflowId, const workflows::WorkflowNode& node);
    std::vector<workflows::WorkflowNode> listNodes(const std::string& workflowId) const;

private:
    std::shared_ptr<Database> db_;
};

class ArtifactRepository {
public:
    explicit ArtifactRepository(std::shared_ptr<Database> db);

    void save(const artifacts::Artifact& artifact);
    artifacts::Artifact get(const std::string& artifactId) const;
    std::vector<artifacts::Artifact> listForJob(const std::string& jobId) const;

private:
    std::shared_ptr<Database> db_;
};

}  // namespace trinity::storage
