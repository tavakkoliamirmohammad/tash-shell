// Pure SLURM argv builders + stdout parsers, factored out of SlurmOps
// so they can be tested directly against golden `sinfo` / `squeue` /
// `sbatch` captures without mocking the SSH transport.
//
// Formats used (for stability — passed to SLURM via `-o`):
//
//   squeue:  %i|%t|%N|%L    jobid|state-compact|nodelist|time-left
//   sinfo:   %P|%t|%D|%G    partition|state-compact|node-count|gres
//   sbatch:  relies on the "--parsable" form (bare jobid, optionally
//            jobid;cluster) but also tolerates the default
//            "Submitted batch job <N>" banner.
//   scancel: no output on success.
//
// %t on squeue / sinfo returns short codes ("R" / "PD" / "CG" / "idle" /
// "alloc" / "mix" / "down"). Our JobState.state and PartitionState.state
// keep those as-is — callers that want pretty prose format upstream.

#ifndef TASH_CLUSTER_SLURM_PARSE_H
#define TASH_CLUSTER_SLURM_PARSE_H

#include "tash/cluster/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace tash::cluster::slurm_parse {

// Parsers — all tolerant: empty input -> empty result; malformed lines
// are dropped rather than aborting.
std::vector<JobState>       parse_squeue(std::string_view output);
std::vector<PartitionState> parse_sinfo (std::string_view output);
std::string                  parse_sbatch_jobid(std::string_view output);

// argv builders. No ssh / no fork; just strings for ISshClient::run.
std::vector<std::string> build_sbatch_argv  (const SubmitSpec&);
std::vector<std::string> build_squeue_argv  ();
std::vector<std::string> build_sinfo_argv   (const std::string& partition);
std::vector<std::string> build_scancel_argv (const std::string& jobid);

}  // namespace tash::cluster::slurm_parse

#endif  // TASH_CLUSTER_SLURM_PARSE_H
