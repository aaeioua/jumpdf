project('jumpdf', 'c',
        version : '1.2.0',
        license : 'MIT',
        meson_version : '>=1.3.0',
        default_options : ['c_std=c99', 'warning_level=3'])

app_name = 'jumpdf'
app_version = meson.project_version()
app_id = 'io.github.b43NnUNF4vidFYFhpqaLWy2ANawtRbMtUXZY9Pf.jumpdf'
developer_id = 'io.github.b43nnunf4vidfyfhpqalwy2anawtrbmtuxzy9pf'

gnome = import('gnome')
i18n = import('i18n')
fs = import('fs')

cc = meson.get_compiler('c')

deps = [
        dependency('gtk4', required : true, version : '>=4.14', include_type : 'system'),
        dependency('poppler-glib', required : true, version : '>=24.02', include_type : 'system'),
        dependency('sqlite3', required : true, version : '>=3.42', include_type : 'system'),
        cc.find_library('m', required : true),
]

if get_option('buildtype') == 'release'
    add_project_arguments('-DG_DISABLE_ASSERT', language: 'c')
endif

subdir('data')
subdir('src')

gnome.post_install(gtk_update_icon_cache: true,
                   update_desktop_database: true)