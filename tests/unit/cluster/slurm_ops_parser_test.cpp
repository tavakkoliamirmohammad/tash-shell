// Parser tests for tash::cluster::slurm_parse — drives each parser
// against golden recordings committed under tests/fixtures/recordings/.
// Plus direct tests of the argv builders (pure string transforms).
//
// TASH_CLUSTER_RECORDINGS_DIR is injected by the build system.

#include <gtest/gtest.h>

#include "tash/cluster/slurm_parse.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef TASH_CLUSTER_RECORDINGS_DIR
#error "TASH_CLUSTER_RECORDINGS_DIR must be defined by the build system"
#endif

using namespace tash::cluster;
using namespace tash::cluster::slurm_parse;

namespace {
std::string read_fixture(const char* relpath) {
    std::ifstream f(std::filesystem::path(TASH_CLUSTER_RECORDINGS_DIR) / relpath);
    std::ostringstream b; b << f.rdbuf(); return b.str();
}
}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// parse_squeue
// ══════════════════════════════════════════════════════════════════════════════

TEST(ParseSqueue, ChpcNormal) {
    auto jobs = parse_squeue(read_fixture("squeue/chpc-normal.txt"));
    ASSERT_EQ(jobs.size(), 3u);
    EXPECT_EQ(jobs[0].jobid,     "1234567");
    EXPECT_EQ(jobs[0].state,     "R");
    EXPECT_EQ(jobs[0].node,      "notch123");
    EXPECT_EQ(jobs[0].time_left, "11:59:30");
    EXPECT_EQ(jobs[2].jobid,     "1234569");
    EXPECT_EQ(jobs[2].time_left, "23:00:00");
}

TEST(ParseSqueue, PendingJobHasEmptyNode) {
    auto jobs = parse_squeue(read_fixture("squeue/chpc-pending.txt"));
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].state, "PD");
    // "(null)" nodelist from squeue normalises to empty.
    EXPECT_EQ(jobs[0].node, "");
    EXPECT_EQ(jobs[0].time_left, "1-00:00:00");
}

TEST(ParseSqueue, MixedStates) {
    auto jobs = parse_squeue(read_fixture("squeue/chpc-mixed-states.txt"));
    ASSERT_EQ(jobs.size(), 6u);
    EXPECT_EQ(jobs[0].state, "R");
    EXPECT_EQ(jobs[1].state, "PD");
    EXPECT_EQ(jobs[2].state, "CG");
    EXPECT_EQ(jobs[3].state, "F");
    EXPECT_EQ(jobs[4].state, "TO");
    EXPECT_EQ(jobs[5].state, "CD");
}

TEST(ParseSqueue, EmptyFileYieldsEmptyVector) {
    auto jobs = parse_squeue(read_fixture("squeue/empty.txt"));
    EXPECT_EQ(jobs.size(), 0u);
}

TEST(ParseSqueue, MalformedLineIsSkipped) {
    // 2-column line is too short; should be dropped.
    auto jobs = parse_squeue("100|R|n1|1:00:00\ngarbage|line\n");
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].jobid, "100");
}

// ══════════════════════════════════════════════════════════════════════════════
// parse_sinfo
// ══════════════════════════════════════════════════════════════════════════════

TEST(ParseSinfo, ChpcGpuPartition) {
    auto ps = parse_sinfo(read_fixture("sinfo/chpc-gpu-partition.txt"));
    ASSERT_EQ(ps.size(), 3u);
    // idle row: 3 nodes, gres count 3
    EXPECT_EQ(ps[0].partition,             "notchpeak-gpu");
    EXPECT_EQ(ps[0].state,                 "idle");
    EXPECT_EQ(ps[0].idle_nodes,            3);
    ASSERT_EQ(ps[0].idle_gres.size(),      3u);
    EXPECT_EQ(ps[0].idle_gres[0],          "gpu:a100:2");
    // alloc / mix rows: idle_nodes = 0
    EXPECT_EQ(ps[1].state,                 "alloc");
    EXPECT_EQ(ps[1].idle_nodes,            0);
    EXPECT_EQ(ps[1].idle_gres.size(),      0u);
    EXPECT_EQ(ps[2].state,                 "mix");
    EXPECT_EQ(ps[2].idle_nodes,            0);
}

TEST(ParseSinfo, DownPartitionStripsReasonAsterisk) {
    auto ps = parse_sinfo(read_fixture("sinfo/partition-down.txt"));
    ASSERT_EQ(ps.size(), 2u);
    EXPECT_EQ(ps[0].state, "down");   // "*"-suffix stripped
    EXPECT_EQ(ps[1].state, "drain");
    EXPECT_EQ(ps[0].idle_nodes, 0);
    EXPECT_EQ(ps[1].idle_nodes, 0);
}

TEST(ParseSinfo, EmptyInputYieldsEmptyVector) {
    EXPECT_TRUE(parse_sinfo("").empty());
    EXPECT_TRUE(parse_sinfo("\n\n").empty());
}

TEST(ParseSinfo, NullGresIsIgnored) {
    auto ps = parse_sinfo("cpu|idle|4|(null)\n");
    ASSERT_EQ(ps.size(), 1u);
    EXPECT_EQ(ps[0].idle_nodes, 4);
    EXPECT_EQ(ps[0].idle_gres.size(), 0u);   // "(null)" doesn't get listed
}

// ══════════════════════════════════════════════════════════════════════════════
// parse_sbatch_jobid
// ══════════════════════════════════════════════════════════════════════════════

TEST(ParseSbatchJobid, BannerFormat) {
    EXPECT_EQ(parse_sbatch_jobid(read_fixture("sbatch/success.txt")), "1234567");
}

TEST(ParseSbatchJobid, ParsableFormat) {
    EXPECT_EQ(parse_sbatch_jobid("9876543\n"),         "9876543");
    EXPECT_EQ(parse_sbatch_jobid("9876543;mycluster"), "9876543");
}

TEST(ParseSbatchJobid, ErrorOutputYieldsEmpty) {
    EXPECT_TRUE(parse_sbatch_jobid(read_fixture("sbatch/invalid-account.txt")).empty());
}

TEST(ParseSbatchJobid, EmptyInputYieldsEmpty) {
    EXPECT_TRUE(parse_sbatch_jobid("").empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// build_*_argv
// ══════════════════════════════════════════════════════════════════════════════

TEST(BuildSbatchArgv, IncludesEveryFieldWhenSet) {
    SubmitSpec s;
    s.cluster   = "c1";
    s.account   = "acc";
    s.partition = "p";
    s.qos       = "q";
    s.gres      = "gpu:a100:1";
    s.time      = "01:00:00";
    s.cpus      = 8;
    s.mem       = "64G";
    s.job_name  = "tash-a100";
    s.wrap      = "sleep infinity";

    const auto argv = build_sbatch_argv(s);
    const std::string joined = [&]{ std::string j; for (const auto& a : argv) { j += a; j += ' '; } return j; }();
    EXPECT_NE(joined.find("sbatch"),                   std::string::npos);
    EXPECT_NE(joined.find("--parsable"),               std::string::npos);
    EXPECT_NE(joined.find("--account=acc"),            std::string::npos);
    EXPECT_NE(joined.find("--partition=p"),            std::string::npos);
    EXPECT_NE(joined.find("--qos=q"),                  std::string::npos);
    EXPECT_NE(joined.find("--gres=gpu:a100:1"),        std::string::npos);
    EXPECT_NE(joined.find("--time=01:00:00"),          std::string::npos);
    EXPECT_NE(joined.find("--cpus-per-task=8"),        std::string::npos);
    EXPECT_NE(joined.find("--mem=64G"),                std::string::npos);
    EXPECT_NE(joined.find("--job-name=tash-a100"),     std::string::npos);
    EXPECT_NE(joined.find("--wrap=sleep infinity"),    std::string::npos);
}

TEST(BuildSbatchArgv, OmitsEmptyFields) {
    SubmitSpec s;
    s.cluster = "c1";
    // everything else empty
    const auto argv = build_sbatch_argv(s);
    // We should have at least "sbatch" and maybe "--parsable", nothing else.
    ASSERT_GE(argv.size(), 1u);
    EXPECT_EQ(argv[0], "sbatch");
    for (const auto& a : argv) {
        EXPECT_EQ(a.find("--account="),   std::string::npos);
        EXPECT_EQ(a.find("--partition="), std::string::npos);
        EXPECT_EQ(a.find("--wrap="),      std::string::npos);
    }
}

TEST(BuildSqueueArgv, UsesMeFilterAndStableFormat) {
    const auto argv = build_squeue_argv();
    ASSERT_FALSE(argv.empty());
    EXPECT_EQ(argv[0], "squeue");
    bool saw_me = false, saw_format = false, saw_header_off = false;
    for (const auto& a : argv) {
        if (a == "--me" || a == "-u" || a.find("=me") != std::string::npos) saw_me = true;
        if (a.find("%i|") != std::string::npos && a.find("%t|") != std::string::npos) saw_format = true;
        if (a == "-h" || a == "--noheader") saw_header_off = true;
    }
    EXPECT_TRUE(saw_me)          << "expected some form of user filter";
    EXPECT_TRUE(saw_format)      << "expected a stable -o format";
    EXPECT_TRUE(saw_header_off)  << "expected -h / --noheader to suppress the header";
}

TEST(BuildSinfoArgv, PartitionAndFormat) {
    const auto argv = build_sinfo_argv("notchpeak-gpu");
    ASSERT_FALSE(argv.empty());
    EXPECT_EQ(argv[0], "sinfo");
    bool saw_partition = false, saw_format = false;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if ((argv[i] == "-p" || argv[i] == "--partition") && i + 1 < argv.size()
            && argv[i+1] == "notchpeak-gpu") saw_partition = true;
        if (argv[i].find("%P") != std::string::npos) saw_format = true;
    }
    EXPECT_TRUE(saw_partition);
    EXPECT_TRUE(saw_format);
}

TEST(BuildScancelArgv, JobIdPositional) {
    const auto argv = build_scancel_argv("1234567");
    ASSERT_EQ(argv.size(), 2u);
    EXPECT_EQ(argv[0], "scancel");
    EXPECT_EQ(argv[1], "1234567");
}
