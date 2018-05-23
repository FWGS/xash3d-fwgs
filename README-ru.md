Xash3D FWGS Android
====
[![Build Status](https://travis-ci.org/FWGS/xash3d-android-project.svg)](https://travis-ci.org/FWGS/xash3d-android-project)
### Пользователям
#### Инструкция по установке
0. Скачайте APK последней версии со страницы релизов репозитория Xash3D FWGS: https://github.com/FWGS/xash3d/releases/latest
1. Откройте и установите APK.
2. Создайте папку /sdcard/xash.
3. Скопируйте папку "valve" от Steam версии Half-Life в /sdcard/xash/. Пример: /sdcard/xash/valve -- игровые данные
4. Запустите игру

#### Запуск модификаций
**Это приложение позволяет запустить только Half-Life и модификации без собственных игровых библиотек.**

**Любая модификация со своими игровыми библиотеками требует отдельно установленное приложение с портированными на Android игровыми библиотеками.**

Например, если вы захотите запустить Half-Life: C.A.G.E.D.

1. Скопируйте папку caged от Steam-версии Half-Life: C.A.G.E.D. в /sdcard/xash.
2. Откройте Xash3D FWGS и добавьте в аргументы командной строки:
> -game caged

Пример для Half-Life: Blue Shift.

1. Скопируйте папку bshift от Steam-версии Half-Life: Blue Shift в /sdcard/xash/.
2. Установите отдельное приложение [отсюда](https://github.com/nekonomicon/BS-android/releases/latest) и запустите.

Портированные на Android модификации cо своими игровыми библиотеками(точнее приложения для их запуска на Android) вы всегда можете найти в [Play Market](https://play.google.com/store/apps/dev?id=7039367093104802597) и на [ModDB](https://www.moddb.com/games/xash3d-android/downloads)

Для большей информации о работающих модификациях, ознакомьтесь с этим [артиклем](https://github.com/FWGS/xash3d/wiki/List-of-mods-which-work-on-Android-and-other-non-Windows-platforms-without-troubles).

#### Баги

Обо всех багах пишите в "Issues" с логом и системной информацией. 

### Разработчикам

+ Для компиляции, запустите `git submodule init && git submodule update --init --recursive`. Иначе у вас будет пустой APK, без библиотек. 
+ ~~Мы используем наш SDL2 форк. Посетите https://github.com/mittorn/SDL-mirror~~. Забудьте. Мы больше не используем SDL2.
