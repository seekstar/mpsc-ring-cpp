from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps


class mpsc_ringRecipe(ConanFile):
    name = "mpsc-ring"
    version = "0.1.1"

    # Optional metadata
    license = "Dual licensed under the Apache License v2.0 and the MIT License"
    author = "Jiansheng Qiu jianshengqiu.cs@gmail.com"
    url = "https://github.com/seekstar/mpsc-ring-cpp"
    description = "Multiple Producer Single Consumer (MPSC) ring"
    topics = ("Channel", "MPSC", "Ring")

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "include/*"

    def requirements(self):
        self.requires("rusty-cpp/[>=0.1.7]")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "mpsc-ring")
        # For header-only packages, libdirs and bindirs are not used
        # so it's necessary to set those as empty.
        self.cpp_info.libdirs = []
        self.cpp_info.bindirs = []
