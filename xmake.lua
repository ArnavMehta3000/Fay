set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release",
		"mode.releasedbg", "mode.profile")

set_project("Fay")
set_version("0.0.1", { build = "%Y%m%d%H%M" })

add_plugindirs(path.join(os.projectdir(), "Plugins"))

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

add_requires("nvrhi", { configs = { validation = true, vulkan = true, d3d12 = is_plat("windows"), } })
add_requires("libsdl3", { alias = "sdl3" })
add_requires("tracy v0.13.1")

add_defines("FAY_ENABLE_PROFILING=" .. (is_mode("profile") and "1" or "0"))

if is_plat("linux") then
	set_toolchains("clang")
elseif is_plat("windows") then
	set_toolchains("msvc")
end

target("Fay")
	set_kind("binary")

	add_packages("nvrhi", "sdl3", "tracy")

	add_undefines("min", "max")

	add_includedirs(
		"Fay",
		"External/SimpleMath",
		{ public = true })

	add_files(
		"Fay/**.cpp", 
		"External/SimpleMath/**.cpp")
	add_headerfiles(
		"Fay/**.h",
		"External/SimpleMath/**.h",
		"External/SimpleMath/**.inl")

	add_extrafiles("Shaders/**.hlsl")

	if is_plat("windows") then
		add_linkdirs(path.join(os.getenv("VULKAN_SDK"),"Lib"))
		add_links("dxgi", "vulkan-1")  -- Other links handled by nvrhi
	elseif is_plat("linux") then
		add_links("vulkan")
		remove_files("Fay/Graphics/DX12/**.cpp")
		remove_headerfiles("Fay/Graphics/DX12/**.h")
	end

	after_build(function (target)
		import("core.project.task")
		task.run("compile_shaders", 
		{
			out_dir  = target:targetdir(),
			src_path = path.join(os.projectdir(), "Shaders"),
			cfg_file = path.join(os.projectdir(), "Shaders", "ShaderBuild.json")
		})
	end)
target_end()