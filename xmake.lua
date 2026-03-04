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

add_requires("nvrhi", { configs = { validation = true, vulkan = true, d3d12 = is_plat("windows"), } })
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

-- Shader compilation task
task("compile_shaders")
	set_category("plugin")
	set_menu({
		usage = "xmake CompileShaders [options]",
		description = "Compile HLSL shaders using DXC",
		options = {
			{'o', "out_dir",  "kv", nil, "Output directory for compiled shaders"},
			{'s', "src_path", "kv", nil, "Path to shader source directory"},
			{'c', "cfg_file", "kv", nil, "Path to ShaderBuild.json config file"},
			{'f', "force",    "k",  nil, "Force recompilation of all shaders"},
		}
	})

	on_run(function ()
		import("core.base.option")
		import("lib.detect.find_tool")
		import("core.base.json")
		import("core.base.process")
		import("core.project.depend")

		-- Find DXC
		local dxc = find_tool("dxc", { check = "--help" })
		assert(dxc, "DXC not found")

		-- Resolve paths
		local shaders_path = assert(option.get("src_path"), "No shader source path provided (--src_path)")
		local cfg_path     = assert(option.get("cfg_file"), "No shader build config path provided (--cfg_file)")
		local out_dir      = assert(option.get("out_dir"), "No output directory provided (--out_dir)")
		local force        = option.get("force")

		out_dir = path.join(out_dir, "Shaders")
		if not os.isdir(out_dir) then
			os.mkdir(out_dir)
		end

		local log_path = path.join(out_dir, "Logs")
		if not os.isdir(log_path) then
			os.mkdir(log_path)
		end

		print("Compiling shaders...")

		-- Load config
		local cfg          = json.loadfile(cfg_path)
		local common_args  = cfg.common_args or {}
		local include_dirs = cfg.include_dirs or {}
		local defines      = cfg.defines or {}
		local entries      = cfg.entries or {}

		-- Add shader source path to includes
		table.insert(include_dirs, shaders_path)

		-- Collect all .hlsl files in include dirs for dependency tracking
		local function collect_includes(dirs)
			local files = {}
			for _, dir in ipairs(dirs) do
				if os.isdir(dir) then
					for _, f in ipairs(os.files(path.join(dir, "**.hlsl"))) do
						table.insert(files, f)
					end
				end
			end
			return files
		end

		local global_includes = collect_includes(include_dirs)

		local dxc_jobs = {}

		for _, entry in ipairs(entries) do
			local source_file  = path.join(shaders_path, entry.source_file or "")
			local output_file  = path.join(out_dir, entry.output_file or "")
			local entry_point  = assert(entry.entry, "Shader entry point not specified for " .. entry.source_file)
			local target       = assert(entry.target, "Shader target not specified for " .. entry.source_file)
			local file_defines = entry.defines or {}

			local stdout_file = path.join(log_path, path.basename(entry.output_file) .. ".log")
			local stderr_file = path.join(log_path, path.basename(entry.output_file) .. ".err")

			-- Dependency check: source file + all potential includes + config file
			local dep_files = { source_file, cfg_path }
			for _, f in ipairs(global_includes) do
				table.insert(dep_files, f)
			end

			local needs_compile = force
			if not needs_compile then
				depend.on_changed(function ()
					needs_compile = true
				end, {
					dependfile = path.join(out_dir, path.basename(entry.output_file) .. ".d"),
					files      = dep_files,
					values     = { entry_point, target, common_args, defines, file_defines },
				})
			end

			-- Also recompile if output is missing
			if not os.isfile(output_file) then
				needs_compile = true
			end

			if not needs_compile then
				cprint("${dim}[Skipped] %s (up to date)${clear}", entry.source_file .. " -> " .. entry.output_file)
				goto continue
			end

			-- Build argument list
			do
				local args = {}

				table.insert(args, "-E")
				table.insert(args, entry_point)

				table.insert(args, "-T")
				table.insert(args, target)

				table.insert(args, "-Fo")
				table.insert(args, output_file)

				for _, a in ipairs(common_args) do
					table.insert(args, a)
				end

				for _, d in ipairs(defines) do
					table.insert(args, "-D")
					table.insert(args, d)
				end

				for _, d in ipairs(file_defines) do
					table.insert(args, "-D")
					table.insert(args, d)
				end

				for _, dir in ipairs(include_dirs) do
					table.insert(args, "-I")
					table.insert(args, dir)
				end

				for _, a in ipairs(entry.extra_args or {}) do
					table.insert(args, a)
				end

				table.insert(args, source_file)

				table.insert(dxc_jobs,
				{
					name        = entry.source_file .. " -> " .. entry.output_file,
					stdout_file = stdout_file,
					stderr_file = stderr_file,
					proc        = process.openv(dxc.program, args,
					{
						stdout = stdout_file,
						stderr = stderr_file
					})
				})
			end

			::continue::
		end

		if #dxc_jobs == 0 then
			print("All shaders up to date")
			return
		end

		local has_errors = false
		for _, job in ipairs(dxc_jobs) do
			local ok, status = job.proc:wait()
			job.proc:close()

			if ok >= 0 and status == 0 then
				cprint("${green}[Success] %s", job.name)
				if os.isfile(job.stdout_file) then
					local content = io.readfile(job.stdout_file)
					if content and content:trim() ~= "" then
						cprint("${yellow}[Warnings] %s:${clear}", job.name)
						print(content)
					end
				end
			else
				has_errors = true
				cprint("${red}[Failed] %s (exit code: %d | status: %d)${clear}", job.name, ok, status)

				if os.isfile(job.stderr_file) then
					local content = io.readfile(job.stderr_file)
					if content and content:trim() ~= "" then
						cprint("${red}[stderr]${clear}")
						print(content)
					end
				end

				if os.isfile(job.stdout_file) then
					local content = io.readfile(job.stdout_file)
					if content and content:trim() ~= "" then
						cprint("${yellow}[stdout]${clear}")
						print(content)
					end
				end
			end
		end

		if has_errors then
			raise("Shader compilation failed")
		else
			print("All shaders compiled successfully")
		end
	end)
task_end()


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

	-- set_pcxxheader("Fay/FayPCH.h")  -- This causes more problems than it helps

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
