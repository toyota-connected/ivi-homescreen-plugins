# Flatpak plugin [WIP]

Ubuntu Package Dependency

```
sudo apt install libflatpak-dev
```

Fedora Runtime Packages

```
sudo dnf install flatpak-devel libxml2-devel
```

Example flatpak CLI usage

```
flatpak list
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-ls
flatpak install org.gnome.Todo
flatpak run org.gnome.Todo
```

Flatpak API reference
https://docs.flatpak.org/en/latest/libflatpak-api-reference.html
