# Skore Engine

[![The MIT License][license-image]][license-url]
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)](https://en.cppreference.com/w/cpp/20)

[license-image]: https://img.shields.io/badge/License-MIT-yellow.svg
[license-url]: https://opensource.org/licenses/MIT

![Scene](Content/Images/Sponza.png)

Skore is an open-source, cross-platform game engine for 2D and 3D game development. Built with modern C++20, it provides developers with a clean, extensible foundation for creating games across multiple platforms.

**🎮 Complete 2D and 3D game development** - unified interface for all your game creation needs  
**⚡ Performance-focused** - lightweight architecture with efficient rendering pipeline  
**🔧 Extensible by design** - modular system allowing easy customization and plugin development  
**🌐 Cross-platform** - deploy to Windows, Linux, macOS and more platforms  
**🛠️ Integrated tooling** - comprehensive editor and development environment  

> ⚠️ **Development Status**: Skore is currently in active development and should be considered alpha software. The API may change between versions. Not recommended for production use at this time.

---

## Design Philosophy

Skore is built around core principles that prioritize developer experience:

**Simplicity First** - Clean, intuitive APIs without unnecessary complexity or tedious configuration  
**Lightweight & Fast** - Minimal dependencies and efficient architecture  
**Easy to Build** - Simple build process with no extra steps required  
**Highly Extensible** - Modular design allowing features to be replaced or extended  
**Developer Friendly** - Comprehensive tooling and integrated development environment  

## Getting Started

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

### Building

```bash
# Clone the repository
git clone https://github.com/SkoreEngine/Skore.git
cd Skore

# Build the engine
mkdir Build && cd Build
cmake ..
cmake --build . --config Release
```

### Your First Project

1. Launch the Skore Editor from the build directory
2. Create a new project or explore the example projects
3. Check out the `Tests` directory for code samples and usage patterns

## Documentation

Comprehensive documentation is in development. Code examples in the `Tests` directory demonstrate key engine concepts and usage patterns.

## Contributing

We welcome contributions of all kinds! Whether you're fixing bugs, improving documentation, adding features, or helping with testing, your help is appreciated.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/your-feature`)
3. Commit your changes (`git commit -m 'Add your feature'`)
4. Push to the branch (`git push origin feature/your-feature`)
5. Open a Pull Request

## Community & Support

- **Issues**: Report bugs and request features on [GitHub Issues](https://github.com/SkoreEngine/Skore/issues)
- **Discussions**: Join conversations about development and usage

## License

Skore Engine is licensed under the MIT License. See the [LICENSE](LICENSE) file for complete details.