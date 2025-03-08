# Goldgen Engine 🚀

![engine](game-engine.png "a title")

**Attribution Details:**  
- **Title of Work:** Goldgen Engine  
- **Creator:** Bogussa Ibrahim  
- **Link to Work:** [https://github.com/Ibra66f/Goldgen](https://github.com/Ibra66f/Goldgen)  
- **Creator Profile:** [https://github.com/Ibra66f](https://github.com/Ibra66f)  
- **Year of Creation:** 2025  

> **Goldgen** is a fork of the Xash3D FWGS engine, a reimplementation of the GoldSource game engine. Building on this robust foundation, Goldgen aims to enhance and modernize the GoldSource experience while preserving compatibility with its classic games.  
> If you find this project helpful, please credit me (Ibra) and the engine in your work. 🙏

---

## GoldSource Engine Enhancement Prototype 🎮

A next-generation fork of the Xash3D FWGS engine, designed to enhance and modernize the GoldSource game engine with new features, optimizations, and potential support for advanced rendering techniques.

For more information on related technology, see:  
- [GoldSrc on Valve Developer Community](https://developer.valvesoftware.com/wiki/GoldSrc)  
- [Xash3D FWGS on GitHub](https://github.com/FWGS/xash3d-fwgs)

---

## Extended Q&A ❓

### Q: What is this project? 🤔  
**A:** Goldgen is a fork of the Xash3D FWGS engine, which reimplements the GoldSource game engine originally derived from Quake. It aims to improve upon its predecessor with modern enhancements while staying true to the GoldSource legacy.

---

### Q: How different is Goldgen from the original Xash3D FWGS? 🔍  
**A:** Goldgen builds on Xash3D FWGS by introducing new features, performance optimizations, and potentially modern rendering capabilities, all while maintaining compatibility with GoldSource games like Half-Life.

---

### Q: Which operating systems are supported? 💻  
**A:** Goldgen supports Windows, Linux, and possibly other platforms inherited from Xash3D FWGS. Check the project's GitHub for the latest compatibility details, and contributions to expand OS support are welcome!

---

### Q: Will the engine support newer rendering APIs like Vulkan or DirectX 11+? 🎨  
**A:** Goldgen plans to explore support for modern rendering APIs like Vulkan to boost performance and visual quality. These features are in development, and community input or contributions are encouraged.

---

### Q: How can I contribute? 🤝  
**A:** We’d love your help! You can contribute by submitting pull requests, reporting bugs, or suggesting ideas. Please see the contribution guidelines on the Goldgen GitHub repository for more details.

---

### Q: Is there any official documentation on how to build this engine? 📚  
**A:** Build instructions and documentation are being developed and will soon be available in the `/Docs` directory of the repository. Watch for updates as the project progresses!

---

### Q: I have more questions or want to discuss ideas. Where can I go? 💬  
**A:** Head over to the GitHub Issues and Discussions sections of the Goldgen repository. We’re open to questions, feedback, and collaboration—feel free to reach out!

---

## Why Goldgen?  
Goldgen was created to build upon the solid foundation of Xash3D FWGS, leveraging its compatibility with GoldSource games and its active community while introducing improvements tailored for modern use cases. This fork represents a step forward in keeping the GoldSource legacy alive and relevant.

---


---

# Goldgen Engine 🚀

![engine](game-engine.png "a title")

**Attribution Details:**  
- **Title of Work:** Goldgen Engine  
- **Creator:** Bogussa Ibrahim  
- **Link to Work:** [https://github.com/Ibra66f/Goldgen](https://github.com/Ibra66f/Goldgen)  
- **Creator Profile:** [https://github.com/Ibra66f](https://github.com/Ibra66f)  
- **Year of Creation:** 2025  

> **Goldgen** is a fork of the Xash3D FWGS engine, a reimplementation of the GoldSource game engine. Building on this robust foundation, Goldgen aims to enhance and modernize the GoldSource experience while preserving compatibility with its classic games.  
> If you find this project helpful, please credit me (Ibra) and the engine in your work. 🙏

---

## GoldSource Engine Enhancement Prototype 🎮

A next-generation fork of the Xash3D FWGS engine, designed to enhance and modernize the GoldSource game engine with new features, optimizations, and potential support for advanced rendering techniques.

For more information on related technology, see:  
- [GoldSrc on Valve Developer Community](https://developer.valvesoftware.com/wiki/GoldSrc)  
- [Xash3D FWGS on GitHub](https://github.com/FWGS/xash3d-fwgs)

---

## Extended Q&A ❓

### Q: What is this project? 🤔  
**A:** Goldgen is a fork of the Xash3D FWGS engine, which reimplements the GoldSource game engine originally derived from Quake. It aims to improve upon its predecessor with modern enhancements while staying true to the GoldSource legacy.

---

### Q: How different is Goldgen from the original Xash3D FWGS? 🔍  
**A:** Goldgen builds on Xash3D FWGS by introducing new features, performance optimizations, and potentially modern rendering capabilities, all while maintaining compatibility with GoldSource games like Half-Life.

---

### Q: Which operating systems are supported? 💻  
**A:** Goldgen supports Windows, Linux, and possibly other platforms inherited from Xash3D FWGS. Check the project's GitHub for the latest compatibility details, and contributions to expand OS support are welcome!

---

### Q: Will the engine support newer rendering APIs like Vulkan or DirectX 11+? 🎨  
**A:** Goldgen plans to explore support for modern rendering APIs like Vulkan to boost performance and visual quality. These features are in development, and community input or contributions are encouraged.

---

### Q: How can I contribute? 🤝  
**A:** We’d love your help! You can contribute by submitting pull requests, reporting bugs, or suggesting ideas. Please see the contribution guidelines on the Goldgen GitHub repository for more details.

---

### Q: Is there any official documentation on how to build this engine? 📚  
**A:** Build instructions and documentation are being developed and will soon be available in the `/Docs` directory of the repository. Watch for updates as the project progresses!

---

### Q: I have more questions or want to discuss ideas. Where can I go? 💬  
**A:** Head over to the GitHub Issues and Discussions sections of the Goldgen repository. We’re open to questions, feedback, and collaboration—feel free to reach out!

---

## Why Goldgen?  
Goldgen was created to build upon the solid foundation of Xash3D FWGS, leveraging its compatibility with GoldSource games and its active community while introducing improvements tailored for modern use cases. This fork represents a step forward in keeping the GoldSource legacy alive and relevant.

---

## Updates

### 08 March 2025

**Key Improvements & Compatibility:**

- **4K Resolution Support:**
  - Uses fixed-size buffers (`row[4096 * 3]`, `rgba[4096 * 4]`) to handle up to 4096-pixel widths.
  - Maintains `int` for dimensions (supports 32-bit sizes).

- **Truecolor Output:**
  - **BMP:** Converts 8-bit indexed textures to 24-bit BGR (palette expanded).
  - **TGA:** Outputs 32-bit RGBA (alpha channel set to 255 for full opacity).

- **Backward Compatibility:**
  - Retains palette-based source data handling (doesn’t break existing .MDL files).
  - Forces `.tga` output for modern workflows while preserving legacy texture extraction logic.

- **Xash3D Safety:**
  - No changes to memory layout of `texture_hdr` or `mstudiotexture_t`.
  - Uses engine’s Q_ functions (`Q_snprintf`, `Q_strncpy`) for path safety.

- **Usage:**
  - Export Textures: Textures are saved as 32-bit `.tga` by default (supports alpha if later added).
  - To use BMP, modify the `WriteTextures` function to retain `.bmp` extensions.

- **Performance:**
  - Buffers are stack-allocated for speed. For textures >4K, switch to dynamic allocation.

- **Alpha Channel:**
  - Currently hardcoded to 255 (opaque). To support transparency, modify the palette loading logic.


<br>
<br>
<br>


#### MenuStrings.cpp Update

**Key Enhancements:**

- **Memory Safety:**
  - Utilizes the RAII pattern with `LocalizedString`.
  - Implements smart pointers for file buffers.
  - Provides automatic cache cleanup.

- **Encoding Support:**
  - Modular encoding detection.
  - UTF-16LE to UTF-8 conversion.
  - CP1251 conversion table integration.

- **Performance:**
  - LRU caching strategy.
  - Bulk parsing using a state machine.
  - Direct memory mapping for faster file access.

- **Compatibility:**
  - Maintains original string IDs.
  - Uses engine file operations.
  - Preserves Xash3D console integration.

- **Error Handling:**
  - Detects missing strings.
  - Fallback to English if needed.
  - Diagnostic logging for troubleshooting.

**Usage Examples:**

- **Basic Localization in Code:**

  ```cpp
  // In menu code
  EngFuncs::DrawText(L("#MAIN_QUIT"), x, y);
  ```

- **Reload Languages via Console Command:**

  ```cpp
  // Console command
  void ReloadLocalization() {
      UI_ShutdownLocalization();
      UI_InitLocalization();
  }
  ```

**Compatibility Notes:**

- Retains original:
  - `L()` macro behavior.
  - String ID numbering.
  - File formats (.txt, .lst) and CP1251 conversion requirements.
  
- Adds:
  - UTF-8/UTF-16 support.
  - Memory safety improvements.
  - Better error recovery.
  - Diagnostic tools.

---

Thank you for your interest in Goldgen Engine! 🎉

**License Information:** MIT License

Created by 2025 Bogussa Ibrahim 🔗

---


Thank you for your interest in Goldgen Engine! 🎉

License Information: MIT License


created by 2025 Bogussa Ibrahim 🔗
