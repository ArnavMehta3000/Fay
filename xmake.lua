set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release",
		"mode.releasedbg", "mode.profile")

set_project("Fay")
set_version("0.0.1", { build = "%Y%m%d%H%M" })

add_plugindirs(path.join(os.projectdir(), "Plugins"))

set_languages("c17", "cxx23")
set_defaultmode("debug")

set_allowedplats("windows")
set_allowedarchs("windows|x64")
set_toolchains("msvc")

set_warnings("all", "extra")

set_policy("build.warning", true)
set_policy("run.autobuild", true)
set_policy("package.install_locally", false)

add_rules("plugin.compile_commands.autoupdate", { outputdir = ".vscode", lsp = "clangd" })

-- Add compilation success to all targets
add_tests("CompileSuccess", { build_should_pass = true, group = "Compilation" })

includes("External/SimpleMath/xmake.lua")

-- For nvrhi
add_repositories("MyRepo https://github.com/ArnavMehta3000/xmake-repo.git")

add_requires("nvrhi", { configs = { validation = true, d3d12 = is_plat("windows"), } })
add_requires("fastgltf")
add_requires("imgui", { configs = { sdl3 = true, win32 = true } })
add_requires("stb")
add_requires("nlohmann_json")
add_requires("libsdl3", { alias = "sdl3" })
add_requires("tracy v0.13.1")

add_defines("FAY_ENABLE_PROFILING=" .. (is_mode("profile") and "1" or "0"))

target("Fay")
	set_kind("binary")
	
	set_policy("build.c++.modules", true)
	add_packages(
		"nvrhi", "sdl3", "tracy", "imgui",
		"fastgltf", "stb", "nlohmann_json")
	add_deps("SimpleMath")
	
	add_undefines("min", "max")
	if is_plat("windows") then
		add_defines("UNICODE", "_UNICODE", "NOMINMAX", "NOMCX", "NOSERVICE", "NOHELP", "WIN32_LEAN_AND_MEAN")
	end

	add_includedirs("Fay", { public = true })

	add_files("Fay/**.cpp")
	add_headerfiles("Fay/**.h")

	add_extrafiles("Shaders/**.hlsl", "Shaders/**.hlsli")

	add_links("dxgi")
	
	after_build( function (target)
		import("core.project.task")
		task.run("compile_shaders",
		{
			out_dir = target:targetdir(),
			src_path = path.join(os.projectdir(), "Shaders"),
			cfg_file = path.join(os.projectdir(), "Shaders", "ShaderBuild.json"),
			target_name = target:name()
		})

		-- Copy assets folder
		local assets_dir = path.join(os.projectdir(), "Assets")
		if os.isdir(assets_dir) then
			os.cp(assets_dir, target:targetdir())
		end
	end)
target_end()
