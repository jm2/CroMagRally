# Cro-Mag Rally

## *The wildest racing game since man invented the wheel!*

This is a port of Pangea Software’s racing game **Cro-Mag Rally** to modern operating systems.

**Download current builds from this fork:** https://github.com/jm2/CroMagRally/releases

![Cro-Mag Rally Screenshot](docs/screenshot.webp)

## About Cro-Mag Rally

> In Cro-Mag Rally you are a speed-hungry caveman named Brog who races through the Stone, Bronze, and Iron Ages in primitive vehicles such as the Geode Cruiser, Bone Buggy, Logmobile, Trojan Horse, and many others. Brog has at his disposal an arsenal of primitive weaponry ranging from Bone Bombs to Chinese Bottle Rockets and Heat Seeking Homing Pigeons.
>
> In addition to single-player racing where one player races against the computer, there are also several different multi-player modes including Tag, Capture the Flag, and Survival. Up to four players can play on a single computer in split-screen mode.

CMR was released in 2000 by Pangea Software as a Mac exclusive, and it was a pack-in game on Macs that came out around that time.

## About this port and fork

This is a port of the original OS 9 version of the game. It aims to provide the best way to experience CMR on today’s computers. It is an “enhanced” version insofar as it fixes bugs that may hinder the experience, and it brings in a few new features in keeping with the spirit of the original game.

This repository builds on [Iliyas Jorio’s modern source port](https://github.com/jorio/CroMagRally). In addition to the desktop targets, this fork maintains Android, iOS, and tvOS builds and restores local-network multiplayer with cross-architecture deterministic simulation.

Some of the new features include:

- Up to 4 players in split-screen multiplayer (up from 2 in the original).
- Up to 4 players over a LAN or Wi-Fi network.
- The UI is subtly animated and has been tweaked to be pleasant to look at on modern widescreens.
- Enable a timer in race modes to hone your racing skills, and keep track of your records in the all-new scoreboard!

### Network play

From the multiplayer menu, one player chooses **Host Net Game** and the others choose **Join Net Game**. Automatic discovery is intended for devices on the same LAN or Wi-Fi subnet. By default, the game uses TCP and UDP port `49959`; allow both through the host firewall if discovery or joining fails. Advanced users can select another port by launching every peer with `--port NUMBER`.

There is no public matchmaking or relay service. Internet play may work through a VPN configured to carry local broadcasts, but it is not the supported configuration. The protocol is intended for trusted local networks, so do not expose the game port directly to the public internet. All peers must run compatible builds of the game.

### More documentation

- [BUILD](BUILD.md) – How to build the game from source
- [CHANGELOG](CHANGELOG.md) – Cro-Mag Rally version history
- [LICENSE](LICENSE.md) – Licensing info (see also below)
- [THIRD-PARTY LICENSES](THIRD-PARTY-LICENSES.md) – Notices for bundled libraries and data
- [SECRETS](SECRETS.md) – Cheat codes!

### Legal info

Cro-Mag Rally © 2000 Pangea Software, Inc. Cro-Mag Rally is a trademark of Pangea Software, Inc. The [original modern port](https://github.com/jorio/CroMagRally) was made and re-released by Iliyas Jorio with permission from Pangea Software, Inc.; this fork is a derivative of that port.

The game and port are licensed under [CC-BY-NC-SA 4.0](LICENSE.md). Bundled third-party components retain their own licenses.

## More Pangea ports

Check out Iliyas Jorio’s ports of [Bugdom](https://github.com/jorio/Bugdom), [Nanosaur](https://github.com/jorio/Nanosaur), [Mighty Mike (Power Pete)](https://github.com/jorio/MightyMike), and [Otto Matic](https://github.com/jorio/OttoMatic).

Those ports are free of charge. You can support Iliyas’s work at https://jorio.itch.io.
