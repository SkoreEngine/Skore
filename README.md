# Skore Engine [PRE-ALPHA]

[![The MIT License][license-image]][license-url]
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)](https://en.cppreference.com/w/cpp/20)
[![Status](https://img.shields.io/badge/status-pre--alpha-red.svg)]()

[license-image]: https://img.shields.io/badge/License-MIT-yellow.svg
[license-url]: https://opensource.org/licenses/MIT

![Scene](Content/Images/Sponza.png)

> **⚠️ IMPORTANT: This is a work-in-progress project. The engine is not ready for production use.**

Skore is an open-source, cross-platform game engine for 2D and 3D game development currently in early development stages. Built with modern C++20, we aim to provide developers with a clean, extensible foundation for creating games across multiple platforms.

**🚧 Current Development Focus:**
**🏗️ Engine foundations** - building core systems and infrastructure  
**⚡ Performance architecture** - designing systems with efficiency in mind  
**✨ Basic features** - implementing essential subsystems and gameplay components  
**🌐 Cross-platform groundwork** - laying the foundation for multi-platform support  

> 🔍 **Development Status**: Skore is in pre-alpha development. Many features are incomplete or missing, the API is unstable and will change frequently. Not suitable for game development yet.

---

## Design Goals

Skore is being built around these core principles:

**Simplicity First** - Clean, intuitive APIs without unnecessary complexity  
**Lightweight & Fast** - Minimal dependencies and efficient architecture  
**Easy to Build** - Simple build process with clear dependencies  
**Highly Extensible** - Modular design allowing features to be replaced or extended  
**Developer Friendly** - Aiming for streamlined workflows and tools

These principles guide our development decisions as we build out the engine's core functionality.

## Development Setup

### Prerequisites

- **CMake** 3.20 or higher
- **C++20 compatible compiler**:
  - Windows: Visual Studio 2019 or newer
  - macOS: Xcode 13+ or AppleClang 13+
  - Linux: GCC 10+ or Clang 12+

### Platform-Specific Dependencies

- **Linux**: Install development packages:
  ```bash
  sudo apt install xorg-dev libglu1-mesa-dev libgtk-3-dev
  ```
- **Windows/macOS**: No additional dependencies required

### Building the Development Version

```bash
# Clone the repository
git clone https://github.com/SkoreEngine/Skore.git
cd Skore

# Build the engine
mkdir Build && cd Build
cmake ..
cmake --build . --config Debug
```

### Exploring the Code

- A robust graphics API abstraction layer is implemented in `Runtime/Source/Skore/Graphics/`
- Core engine systems are being developed in the `Runtime/Source/Skore/` directory
- Check out the `Tests` directory for examples of implemented features
- The project uses a modular architecture with clear separation of concerns

## Documentation

Documentation is planned but not yet available as the API is still evolving. The best way to understand the current architecture is to review the source code and the examples in the `Tests` directory. We'll begin formal documentation once the core APIs stabilize.

## Contributing

Skore is in early development and we welcome contributions! The engine is taking shape and there are many areas where help is needed:

- **Core Systems Development**: Help implement fundamental engine systems and basic features
- **Engine Architecture**: Contribute to the design and implementation of core components
- **Build System**: Improvements to CMake configuration and cross-platform support
- **Testing**: Help identify and fix bugs in the existing codebase

If you're interested in contributing:

1. Check the issues page for tasks labeled "good first issue"
2. Fork the repository
3. Create your feature branch (`git checkout -b feature/your-feature`)
4. Commit your changes (`git commit -m 'Add your feature'`)
5. Push to the branch (`git push origin feature/your-feature`)
6. Open a Pull Request

Please note that the codebase is evolving rapidly, so coordinate with us before tackling major features.

## Development Roadmap & Communication

- **Issues**: Report bugs and suggest features on [GitHub Issues](https://github.com/SkoreEngine/Skore/issues)
- **Discussions**: Discuss architecture and implementation details on GitHub Discussions
- **Roadmap**: Check the project board for upcoming features and current development focus

We're building this engine in the open and value feedback from developers interested in the project's direction.

## License

Skore Engine is licensed under the MIT License. See the [LICENSE](LICENSE) file for complete details.