# ivi-homescreen-plugins-template

This folder contains a template for creating a plugin for the ivi homescreen. To create a new plugin:

- **A**: (single plugin) Use this folder as a template to develop your plugin. You can rename the folder to match your plugin name.
- **B**: (multiple plugins) Include this repository as a submodule in your own repository:
    1. Create a `plugins` folder in your repository
    2. Next to it, add this submodule and name the folder `template`
    3. Inside each plugin folder under `plugins/<your-plugin-name>`:
        - Symlink the `include/homescreen-plugin` header to the `include/homescreen-plugin` header in the `template` folder
        - Symlink the `CMakeLists.txt` to the `CMakeLists.txt` in the `template` folder
        - Copy and modify the `plugin.cmake` file to match your plugin name and source files
        - Create your plugin source files and include the `homescreen-plugin` headers as needed

