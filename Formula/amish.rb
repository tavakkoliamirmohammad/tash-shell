class Amish < Formula
  desc "A lightweight Unix shell with pipes, redirection, and job control"
  homepage "https://github.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface"
  url "https://github.com/tavakkoliamirmohammad/UNIX-Command-Line-Interface/archive/refs/tags/v1.0.0.tar.gz"
  # sha256 will be filled when the release is created
  license "MIT"

  depends_on "cmake" => :build
  depends_on "readline"

  def install
    system "cmake", "-B", "build", "-DBUILD_TESTS=OFF",
           "-DCMAKE_BUILD_TYPE=Release", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/shell.out" => "amish"
  end

  test do
    assert_match "GoodBye", shell_output("echo exit | #{bin}/amish")
  end
end
