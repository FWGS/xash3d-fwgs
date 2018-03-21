====
Xash3D Android
====
[![Build Status](https://travis-ci.org/FWGS/xash3d-android-project.svg)](https://travis-ci.org/FWGS/xash3d-android-project)
### Пользователи
#### Инструкция по установке
0. Скачайте APK последней версии с Github. https://github.com/SDLash3D/xash3d-android-project/releases/latest
1. Откройте и установите APK.
2. Create папку /sdcard/xash.
3. Скопируйте папку "valve" со Steam версии Half-Life в /sdcard/xash/. Пример: /sdcard/xash/valve -- игровые данные
4. Откройте игру 

#### Запуск модификации
**Этот порт поддерживает только Half-Life и моды без собственных библиотек. **

Например, вы также можете запустить Half-Life Uplink. 

1. Скопируйте папку модификации в /sdcard/xash
2. Откройте игру и добавьте в коммандную строку: 
> -game "НазваниеПапкиМода"

#### Баги

Обо всех багах пишите в "Issues" с логом и системной информацией. 

### Разработчикам

+ Для компиляции, запустите `git submodule init && git submodule update`. Иначе у вас будет пустой APK, без библиотек. 
+ ~~Мы используем наш SDL2 форк. Посетите https://github.com/mittorn/SDL-mirror~~. Необязательно. Мы больше не используем SDL2.
