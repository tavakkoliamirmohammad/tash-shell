class Tash < Formula
  desc "Modern Unix shell with syntax highlighting, autosuggestions, AI assistance, and themes"
  homepage "https://github.com/tavakkoliamirmohammad/tash-shell"
  url "https://github.com/tavakkoliamirmohammad/tash-shell/archive/refs/tags/v2.0.0.tar.gz"
  # sha256 is filled in after the release tarball is cut — run
  # `shasum -a 256 <tarball>` against the generated archive and paste
  # the digest here, or let `brew bump-formula-pr` do it. The livecheck
  # stanza below tells `brew livecheck` / `brew bump-formula-pr` which
  # upstream tag to track.
  license "MIT"
  head "https://github.com/tavakkoliamirmohammad/tash-shell.git", branch: "master"

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on "cmake" => :build
  depends_on "curl"
  depends_on "nlohmann-json"
  depends_on "sqlite"

  def install
    system "cmake", "-B", "build", "-DBUILD_TESTS=OFF",
           "-DCMAKE_BUILD_TYPE=Release", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/tash.out" => "tash"
  end

  test do
    # Confirm the binary launches and self-reports the full feature set.
    # If this assertion fails the build regressed — most likely an
    # optional dep wasn't found at configure time.
    version_output = shell_output("#{bin}/tash --version")
    assert_match "tash ", version_output
    assert_match "+ai", version_output
    assert_match "+sqlite-history", version_output
    # Core interactive loop still round-trips.
    assert_match "GoodBye", shell_output("echo exit | #{bin}/tash")
  end
end
