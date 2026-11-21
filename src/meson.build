sources = [
    'main.c',
    'config.c',
    'toml.c',
    'utils.c',
    'app.c',
    'window.c',
    'statusline.c',
    'outline.c',
    'outline_panel.c',
    'statusline_layout.c',
    'statusline_format.c',
    'mark_store.c',
    'viewer.c',
    'renderer.c',
    'input.c',
    'viewer_info.c',
    'document.c',
    'viewer_cursor.c',
    'viewport.c',
    'search.c',
    'page_links.c',
    'viewer_mark_group.c',
    'viewer_mark_manager.c',
]

conf_data = configuration_data()
conf_data.set('app_name', app_name)
conf_data.set('app_id', app_id)
configure_file(input : 'project_config.h.in',
           output : 'project_config.h',
           configuration : conf_data)

executable(app_name,
       sources : sources,
       dependencies : deps,
       install : true)