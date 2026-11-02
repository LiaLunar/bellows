#pragma once

#include "../benchmark.hpp"
#include "../common.hpp"

#include "symbols.gen.hpp"  // sym::<name>, written by anvil gen from plugin/symbols.toml

namespace sb {

// Each Install* requests its changes through the Anvil host and logs the
// outcome. Anvil verified every symbol of plugin/symbols.toml before the DLL
// was loaded, and the changes are enabled together when load() returns.
// Returns true when the module is active. A refused change throws
// anvil::Error; the module's sb::Undo removes what it already requested, so a
// failed module leaves the game unchanged for that module only.
bool InstallMimalloc(anvil::Host& host, Mode mode);
bool InstallVerifyPathListing(anvil::Host& host, Mode mode);
bool InstallExistsIndex(anvil::Host& host, Mode mode);
bool InstallTriggerIntern(anvil::Host& host, Mode mode);
bool InstallMetascriptSubst(anvil::Host& host, Mode mode);
bool InstallMetascriptBlocks(anvil::Host& host, Mode mode);
bool InstallOpenWarmup(anvil::Host& host, Mode mode);
bool InstallAttrCache(anvil::Host& host, Mode mode);
bool InstallRamPrefetch(anvil::Host& host, Mode mode);
void SetRamPrefetchBudget(std::uint64_t megabytes);  // before InstallRamPrefetch
bool InstallZipProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallTokenDictProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallTokenDictCache(anvil::Host& host, Mode mode);
bool InstallRangeProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallRangeWitness(anvil::Host& host, Mode mode);
bool InstallFramePacing(anvil::Host& host, Mode mode);
bool InstallModifierMerge(anvil::Host& host, Mode mode);
bool InstallProvinceModifierView(anvil::Host& host, Mode mode);
bool InstallSaveCompress(anvil::Host& host, Mode mode);
bool InstallTickProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallArmySearchProfile(anvil::Host& host, Mode mode);  // diagnostic
bool InstallScoreProbe(anvil::Host& host, Mode mode);  // diagnostic
bool InstallMonthlyProbe(anvil::Host& host, Mode mode);  // diagnostic
// Before InstallFramePacing; 0 ms = count only. draw_on_input: the game's interval while input waits.
void SetFramePacingInterval(std::int64_t milliseconds, bool draw_on_input);
// Before InstallSaveCompress: save_compress_parallel, 0 off, 1 on, 2 verify.
void SetSaveCompressParallel(int mode);

// mimalloc = "startup": returns the pages freed since the switch to Windows
// (on the main thread, at the game-ready and session events). Without
// allocations from mimalloc nothing else starts its delayed purge.
void CollectMimalloc(const char* reason);
// The same once 64 MiB of mimalloc blocks were freed since the last collect
// (on the main thread, at a frame); true when it collected.
bool CollectMimallocIfDue();

// Stops learning and saves the access profile (after the game-ready event).
void FinishOpenWarmup();

// Stops prefetching, frees unused buffers and saves the archive-member profile (after the game-ready event).
void FinishRamPrefetch();

// Writes the counters of every installed module to the log and sets its
// benchmark fields (benchmark.hpp).
void ReportMimalloc();
void ReportVerifyPathListing();
void ReportExistsIndex();
void ReportTriggerIntern();
void ReportMetascriptSubst();
void ReportMetascriptBlocks();
void ReportOpenWarmup();
void ReportAttrCache();
void ReportRamPrefetch();
void ReportZipProfile();
void ReportTokenDictProfile();
void ReportTokenDictCache();
void ReportRangeProfile();
void ReportRangeWitness();
void ReportModifierMerge();
void ReportProvinceModifierView();
void ReportSaveCompress();
void ReportFramePacing();

}  // namespace sb
