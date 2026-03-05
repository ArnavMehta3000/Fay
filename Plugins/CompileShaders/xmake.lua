-- Shader compilation task
task("compile_shaders")
	set_category("plugin")
	set_menu(
	{
		usage = "xmake CompileShaders [options]",
		description = "Compile HLSL shaders using DXC",
		options =
		{
			{'o', "out_dir",  "kv", nil, "Output directory for compiled shaders" },
			{'s', "src_path", "kv", nil, "Path to shader source directory"       },
			{'c', "cfg_file", "kv", nil, "Path to ShaderBuild.json config file"  },
			{'f', "force",    "k",  nil, "Force recompilation of all shaders"    },
		}
	})

	on_run("main")

task_end()
