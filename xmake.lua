set_xmakever("3.0.5")
add_rules("mode.debug", "mode.release", "mode.releasedbg")

set_project("Fay")
set_version("0.0.1", { build = "%Y%m%d%H%M" })

set_languages("c17", "cxx23")
set_defaultmode("debug")

set_warnings("all", "extra")

set_policy("build.warning", true)
set_policy("run.autobuild", true)

add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})

-- Add compilation success to all targets
add_tests("CompileSuccess", { build_should_pass = true, group = "Compilation" })

-- For nvrhi
add_repositories("MyRepo https://github.com/ArnavMehta3000/xmake-repo.git")

add_requires("nvrhi")
add_requires("libsdl3 v3.4.0", { alias = "sdl3" })

target("fay")
    set_kind("binary")

    add_packages("nvrhi", "sdl3")
	
	add_includedirs("Fay", { public = true })
    add_files("Fay/**.cpp")
    add_headerfiles("Fay/**.h")

    set_pcxxheader("Fay/FayPCH.h")
	add_forceincludes("Fay/FayPCH.h", { public = true })
target_end()
