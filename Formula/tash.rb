class Tash < Formula
  desc "A modern Unix shell with syntax highlighting, autosuggestions, and Catppuccin colors"
  homepage "https://github.com/tavakkoliamirmohammad/tash-shell"
  url "https://github.com/tavakkoliamirmohammad/tash-shell/archive/refs/tags/v1.0.0.tar.gz"
  # sha256 will be filled when the release is created
  license "MIT"

  depends_on "cmake" => :build

  def install
    system "cmake", "-B", "build", "-DBUILD_TESTS=OFF",
           "-DCMAKE_BUILD_TYPE=Release", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/tash.out" => "tash"
  end

  test do
    assert_match "GoodBye", shell_output("echo exit | #{bin}/tash")
  end
end
