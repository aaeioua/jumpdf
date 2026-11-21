# Configuration

jumpdf reads its settings from `config.toml` when it starts:

- `~/.config/jumpdf/config.toml` or `$XDG_CONFIG_HOME/jumpdf/config.toml`
- Flatpak: `~/.var/app/io.github.b43NnUNF4vidFYFhpqaLWy2ANawtRbMtUXZY9Pf.jumpdf/config/jumpdf/config.toml`

Every setting is optional, so the file only needs the ones you want to change. A setting that is left out or with an incorrect value uses its default.

## Quick start

Built from source:

```sh
mkdir -p ~/.config/jumpdf
cp /usr/local/share/jumpdf/config.toml ~/.config/jumpdf/
```

Flatpak:

```sh
app=io.github.b43NnUNF4vidFYFhpqaLWy2ANawtRbMtUXZY9Pf.jumpdf
mkdir -p ~/.var/app/$app/config/jumpdf
flatpak run --command=cp $app /app/share/jumpdf/config.toml ~/.var/app/$app/config/jumpdf/
```

## Settings

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `file_chooser_initial_folder_path` | String | `"~/Documents"` | The folder the file chooser (<kbd>o</kbd>) opens in. A leading `~` stands for your home folder. |
| `steps` | Integer | `15` | How many steps it takes to scroll through one page. <kbd>j</kbd>, <kbd>k</kbd>, <kbd>h</kbd> and <kbd>l</kbd> move one step, so a higher value scrolls in smaller steps. |
| `min_scale` | Number | `0.3` | The smallest scale that zooming out reaches, where `1.0` is 100%. |
| `scale_step` | Number | `0.1` | How much <kbd>+</kbd> and <kbd>-</kbd> change the scale, where `1.0` is 100%. |
| `auto_reload` | Boolean | `false` | Reload a document when its file changes. |

## Status line

The status line has a left, a middle and a right section. Each section shows its items in order, with `statusline_separator` between them. An item with nothing to show, such as `"Pending keys"` when no keys are pending, is left out along with its separator.

| Setting | Type | Default |
| --- | --- | --- |
| `statusline_left` | Array of items | `["Page", "Search", "Pending keys"]` |
| `statusline_middle` | Array of items | `[]` |
| `statusline_right` | Array of items | `["Center mode", "Scale", "Mark selection"]` |
| `statusline_separator` | String | `" \| "` |

`[]` leaves a section empty. An item name jumpdf doesn't recognize prints an error and is skipped, and the rest of the section still shows. Item names are case-sensitive.

| Item | Shows |
| --- | --- |
| `"Page"` | The current page and the number of pages. |
| `"Search"` | The search text and which match you are on. |
| `"Pending keys"` | The keys of a command you have started typing. |
| `"Center mode"` | `Center` while center mode is on. |
| `"Scale"` | The current scale. |
| `"Mark selection"` | The current group and mark as `group:mark`. |
