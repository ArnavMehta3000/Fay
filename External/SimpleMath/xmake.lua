if is_plat("linux") then
    add_requires("directxmath")
end

target("SimpleMath")
    if is_plat("linux") then
        add_packages("directxmath", { public = true })
    end

    set_kind("static")
    add_files("src/SimpleMath.cpp")
    add_headerfiles("include/SimpleMath.h", "include/SimpleMath.inl")

    add_includedirs("include", { public = true })

target_end()
