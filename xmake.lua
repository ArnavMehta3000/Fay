set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release",
        "mode.releasedbg", "mode.profile")

set_project("Fay")
set_version("0.0.1", { build = "%Y%m%d%H%M" })

set_languages("c17", "cxx23")
set_defaultmode("debug")

set_warnings("all", "extra")

set_policy("build.warning", true)
set_policy("run.autobuild", true)
set_policy("package.install_locally", true)

add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})

-- Add compilation success to all targets
add_tests("CompileSuccess", { build_should_pass = true, group = "Compilation" })

-- For nvrhi
add_repositories("MyRepo https://github.com/ArnavMehta3000/xmake-repo.git")

add_requires("nvrhi", { configs = { validation = true, vulkan = true, d3d12 = true } })
add_requires("libsdl3", { alias = "sdl3" })
add_requires("tracy v0.13.1")

if is_mode("profile") then
    add_defines("FAY_ENABLE_PROFILING=1")
else
    add_defines("FAY_ENABLE_PROFILING=0")
end

if is_plat("linux") then
    set_toolchains("clang")
elseif is_plat("windows") then
    set_toolchains("msvc")
end

target("Fay")
    set_kind("binary")

    add_packages("nvrhi", "sdl3", "tracy")

	add_includedirs("Fay", { public = true })
    add_files("Fay/**.cpp")
    add_headerfiles("Fay/**.h")

    set_pcxxheader("Fay/FayPCH.h")

    if is_plat("windows") then
        add_links("dxgi")  -- Other links handled by nvrhi
    end
target_end()
