#pragma once
#include "project.h"
namespace projects {
enum class Operation { None, Save, SaveAs, Load, New, Scan, Fixture };
enum class Handoff { Idle, Snapshot, SnapshotReady, Apply, ApplyReady };
extern std::atomic<Handoff> handoff;
extern std::atomic<bool> restoring;
extern std::atomic<unsigned> changes, saved_revision, revision,
    applied_revision;
extern std::atomic<bool> view_ready;
extern std::atomic<bool> keep_resident;
extern project::State *staging;
// Audio-owned remembered references, independent of UI preview and PCM.
extern char references[16][96];
void initialize();
bool request(Operation operation, const char *name = nullptr);
bool busy();
bool locked();
bool dirty();
void edited(app::Kind kind);
void assigned(unsigned track, const char *name);
// Called exclusively by the existing storage task; owns all filesystem I/O.
void poll();
void describe(unsigned index, char *name, unsigned size);
unsigned count();
void status(char *text, unsigned size);
void current(char *text, unsigned size);
void remember_name(const char *name);
void measured(bool snapshot, unsigned us);
extern std::atomic<unsigned> snapshot_blocks, snapshot_max, apply_blocks,
    apply_max, fixtures, fixture_errors;
} // namespace projects
