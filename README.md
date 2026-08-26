# AvirA Steam Tool

Переписанный на C++ чекер Steam-аккаунтов с кастомным GUI на ImGui — без Python, собирается обычным MSVC.

Основано на https://github.com/CinAlix/Steam-Account-Checker, но полностью на WinAPI/DirectX11/WinHTTP/BCrypt, с каталогом аккаунтов и авто-входом в Steam.

![AvirA](AvirA%20Logo.png)
<img width="1180" height="760" alt="image" src="https://github.com/user-attachments/assets/fde06e43-645c-4b7a-b2b7-ce494f1d6db0" />

## Возможности

- **Проверка** списков `login:password` (и `login:pass:mail:mailpass` — берутся первые два поля) через `steamcommunity.com/login/getrsakey` → RSA PKCS1 v1.5 → `dologin`
- Классификация: `VALID` / `STEAMGUARD (2FA)` / `BAD` / `ERR` / `RATE`
- **Многопоток** (1–64) с ретраями, джиттером и обработкой 429/captcha/rate-limit
- **Прокси** — список в памяти, ротация на каждый ретрай (`host:port` или `user:pass@host:port`)
- **Каталог** — все `VALID`/`2FA` автоматом попадают в список, сохраняются в `data/accounts.txt` и `data/hits.txt`, всегда под рукой
- **Авто-вход** — кнопка «Войти» убивает текущий `steam.exe`, чистит `HKCU\Software\Valve\Steam\AutoLoginUser` и запускает `steam.exe -login "user" "pass"`. Если уже залогинен в тот же аккаунт — просто уведомление
- **Лог проверки** — живой список с временем, статусом и сообщением
- GUI: безрамочное окно (drag за шапку), кастомный стиль (не дефолтный ImGui), фиолетовая палитра, анимации ховеров/переходов, тултипы у всех иконок, скроллбары

## Как работает

1. Для каждого `user` POST на `/login/getrsakey/` → `publickey_mod`, `publickey_exp`, `timestamp`
2. Пароль шифруется RSA (BCRYPT_RSAPUBLIC_BLOB + `BCryptEncrypt` `BCRYPT_PAD_PKCS1`) и кодируется Base64
3. POST на `/login/dologin/` с `rsatimestamp` и кукой сессии (`Set-Cookie` из первого запроса пробрасывается во второй — иначе Steam отвечает «incorrect password» для валида)
4. Ответ: `success==true` → VALID; `emailauth_needed || requires_twofactor` → 2FA (тоже валид); `captcha_needed`/429/`rate`/`try again` → RATE (ретрай); иначе BAD

Сессия одна на аккаунт (общий `WinHttpOpen` для двух запросов), таймауты 12с.

## Требования

- Windows 10/11
- Visual Studio 2022 (v143) — больше никаких компиляторов
- Установленный Steam (путь читается из `HKCU\Software\Valve\Steam\SteamExe` / `HKLM\...`)

Зависимости уже в репо: `ext/imgui` (v1.91.8) + бэкенды DX11/Win32. Линкуется `d3d11`, `dwmapi`, `winhttp`, `bcrypt`, `ole32`, `comdlg32`.

## Сборка

```bat
# открой
AvirASteamTool.sln
# выбери Release | x64 → Build → Run
# бинарь
bin\x64\Release\AvirASteamTool.exe
```

Или консолью:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" AvirASteamTool.vcxproj /p:Configuration=Release /p:Platform=x64
```

Лого `AvirA Logo.png` должен лежать рядом с `exe` (автоматически копируется при сборке) или в корне проекта — грузится через WIC.

## Использование

1. Запусти `AvirASteamTool.exe`
2. Вкладка **Проверка** → блок *Аккаунты для проверки*:
   - вставь строки или `Загрузить .txt`
   - форматAccepted на строку:
     ```
     login:password
     login:password:email:emailpass   # берутся первые два
     ```
   - плейсхолдер подсказывает вид
   - чип справа показывает сколько распарсилось
3. Подтяни `proxy.txt` в **Настройки → Загрузить proxy.txt** (каждому ретраю — рандомный прокси). Без прокси — прямой коннект
4. Выставь **Потоки** (слайдер) — 8 по умолчанию, 12–16 норм без прокси, с ротацией можно больше
5. `Запустить проверку` → прогресс-бар, счётчики VALID/2FA/BAD/ERR, лог справа (чистится иконкой 🗑)
6. Валидные сразу появляются в **Аккаунты** и пишутся на диск
7. В **Аккаунты** — поиск, бейдж `VALID`/`2FA`, `Войти`, копирование логина/пароля, удаление

## Файлы данных

Рядом с `exe` создаётся `data\`:

```
data\
  accounts.txt  # user<TAB>valid/guard<TAB>password
  hits.txt      # user:pass (экспорт)
  proxies.txt   # последняя загрузка прокси
  settings.ini  # threads/preset/sound/mask/autoexport
```

`accounts.txt` загружается при старте. Экспорт в каталоге — кнопка 💾.

## Вход в Steam

`steamctl.cpp:LoginToAccount`:
- `GetSteamPath()` → `HKCU\Software\Valve\Steam\SteamExe`
- `GetSteamAutoLoginUser()` → `HKCU\Software\Valve\Steam\AutoLoginUser`
- если уже `AutoLoginUser == target && IsSteamRunning()` → `AlreadyActive`
- иначе `KillSteam()` (Toolhelp32 → `TerminateProcess`), `RegDeleteValue(AutoLoginUser)`, `CreateProcess("\"...\steam.exe\" -login \"user\" \"pass\"")`

Steam должен быть установлен. Логин с 2FA потребует код Guard в клиенте.

## Тонкая настройка

- Пресеты акцента в **Настройки → Внешний вид** (6 вариантов, хранится в `settings.ini`)
- Тогглы: звук при находке, скрывать пароли, автоэкспорт
- Все цвета — `src/theme.cpp:InitPalette`, скругления и паддинги — `ApplyStyle`

## Известные ограничения

- `steamcommunity.com` может отвечать капчей/rate-limit при агрессии — чекер это ловит как `RATE` и ретраит, но без валидных прокси высокий `threads` даст много `ERR`
- RSA только PKCS1, ключ 2048 бит — пароль длиннее 214 байт не шифруется
- Куки сессии обязательны — не удаляй проброс `Set-Cookie` в `steamcheck.cpp`

## Дисклеймер

Только в учебных целях. Автор не несёт ответственности за misuse. Не используй чужие аккаунты без согласия владельца.

## Лицензия

MIT — как у оригинала. ImGui — MIT.
