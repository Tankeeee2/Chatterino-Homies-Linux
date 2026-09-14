![chatterinoLogo](./resources/icon.png)

# Chatterino Homies (Linux Edition)

Chatterino 7 (SevenTV v7.5.5+) fork adapted and maintained for Linux, integrating features from the Homies community.

> **Disclaimer**: This is a personal project to maintain Linux support for Chatterino Homies. I am not the original author of Chatterino, SevenTV, or Chatterino Homies; all credit belongs to their respective creators ([Chatterino](https://github.com/Chatterino/chatterino2), [SevenTV](https://github.com/SevenTV/chatterino7), and [itzAlex](https://github.com/itzAlex/chatterino7)).

## Features

### Homies
- **Badges**: Custom Homies user badges (tiers 1, 2, and 3).
- **Emotes**: Global and channel Homies emotes with autocompletion and emote picker integration.
- **Channel filters for highlights**: Restrict or exclude highlight phrases and users to specific channels.
- **Homies settings tab**: Dedicated settings page for Homies badges, emotes, and mention formats.
- **Moderator card**: Moderation tools in user cards.

### SevenTV
- Name paints (animated and gradient username styling)
- Personal emotes
- Animated profile avatars
- 4x image support (7TV & FFZ)

## Installation (Arch Linux)

Build and install using `makepkg`:

```bash
git clone https://github.com/Tankeeee2/Chatterino-Homies-Linux.git
cd Chatterino-Homies-Linux
makepkg -si
```

### Wayland & dead keys (accents)
For native Wayland support or to fix accent/dead key input on Wayland compositors:

```bash
sudo pacman -S --needed qt6-wayland fcitx5-qt
```

## Links

- Chatterino: [Website](https://chatterino.com) | [Wiki](https://wiki.chatterino.com) | [Discord](https://discord.gg/7Y5AYhAK4z) | [GitHub](https://github.com/Chatterino/chatterino2)
- SevenTV: [Website](https://7tv.app) | [Discord](https://discord.gg/7TV) | [GitHub](https://github.com/SevenTV/chatterino7)
- Homies: [itzAlex/chatterino7](https://github.com/itzAlex/chatterino7)
