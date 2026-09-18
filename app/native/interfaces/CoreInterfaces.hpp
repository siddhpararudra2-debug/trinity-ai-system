// Trinity — canonical core interface set (brief §Core interfaces).
//
// This header is the one place that names every contract the application is
// built around, so an implementer (or the future model layer) can see the whole
// boundary at a glance instead of hunting through modules.
//
//   IEngine         engines/IEngine        — CAD, math and the scaffold engines
//   IJob            jobs/IJob              — a single async job handle
//   IJobManager     jobs/IJobManager       — submit/query/control jobs
//   IArtifactStore  artifacts/IArtifactStore — the single writer of artifacts
//   IProjectStore   projects/IProjectStore — project lifecycle
//   IValidator      validation/IValidator  — GENERATED -> VALIDATED -> VERIFIED
//   IWorkflow       workflows/IWorkflow    — dependency-ordered engine steps
//   IModelProvider  model/IModelProvider   — the replaceable model boundary
//   IPlugin         plugins/IPlugin        — plugin lifecycle
//   ICommand        commands/ICommand      — palette/menu command unit
//
// Every one of these has a real, tested implementation in this build; none of
// them is a stub. The model-related interfaces ship with deterministic
// null/mock implementations only (brief §Critical Rule).
#pragma once

#include "../artifacts/Artifact.hpp"
#include "../commands/CommandRegistry.hpp"
#include "../engines/Engine.hpp"
#include "../jobs/JobSystem.hpp"
#include "../model/ModelInterfaces.hpp"
#include "../plugins/PluginManager.hpp"
#include "../projects/Project.hpp"
#include "../validation/ValidationEngine.hpp"
#include "../workflows/Workflow.hpp"

namespace trinity::interfaces {

using IEngine = engines::IEngine;
using IJob = jobs::IJob;
using IJobManager = jobs::IJobManager;
using IArtifactStore = artifacts::IArtifactStore;
using IProjectStore = projects::IProjectStore;
using IValidator = validation::IValidator;
using IWorkflow = workflows::IWorkflow;
using IModelProvider = model::IModelProvider;
using IPlugin = plugins::IPlugin;
using ICommand = commands::ICommand;

}  // namespace trinity::interfaces
