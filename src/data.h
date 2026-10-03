#pragma once
// Map list and translations for Loadout Launcher.
// Map codes are taken 1:1 from the original Loadout Reloaded patcher.

enum Lang { L_RU = 0, L_EN = 1, L_ES = 2, L_COUNT };

enum TextId {
    T_GAME_MISSING, T_ST_LAUNCHING, T_ST_WAITING, T_ST_READY, T_ST_CLOSED, T_ST_ERROR,
    T_BTN_START, T_STEP_MAP, T_STEP_TIME, T_STEP_MODE, T_DAY, T_NIGHT, T_SEC_MAIN, T_SEC_OTHER,
    T_BACK, T_RETURN, T_HINT_APPLY, T_MAP_CODE, T_COPIED, T_RESET, T_START_WARN, T_CUSTOM_HINT, T_TOO_SHORT, T_TOO_LONG,
    T_SETTINGS, T_SERVER, T_SERVER_HINT, T_DEFAULT, T_SAVE, T_SERVER_INVALID, T_GAME_FOLDER, T_CLOSE,
    T_NO_MODE, T_SENT, T_PENDING, T_CHOOSE_TIME, T_CHOOSE_MODE, T_CUSTOM_CODE, T_TIME, T_MODE, T_MAP, T_PVE_TIP, T_SAVED, T_LAUNCH_FAIL, T_RESTART_NEEDED,
    T_COUNT
};

static const char* TXT[T_COUNT][L_COUNT] = {
    /* T_GAME_MISSING */ { "Loadout.exe не найден", "Loadout.exe not found", "No se encontró Loadout.exe" },
    /* T_ST_LAUNCHING */ { "Игра запускается…", "Starting the game…", "Iniciando el juego…" },
    /* T_ST_WAITING */ { "Подключаем игру к серверу…", "Connecting the game to the server…", "Conectando el juego al servidor…" },
    /* T_ST_READY */ { "Игра готова", "Game is ready", "El juego está listo" },
    /* T_ST_CLOSED */ { "Игра закрыта", "Game is closed", "El juego está cerrado" },
    /* T_ST_ERROR */ { "Игра не запустилась", "The game did not start", "El juego no se inició" },
    /* T_BTN_START */ { "Запустить игру", "Launch game", "Iniciar juego" },
    /* T_STEP_MAP */ { "Карта", "Map", "Mapa" },
    /* T_STEP_TIME */ { "Время суток", "Time of day", "Hora del día" },
    /* T_STEP_MODE */ { "Режим", "Mode", "Modo" },
    /* T_DAY */ { "День", "Day", "Día" },
    /* T_NIGHT */ { "Ночь", "Night", "Noche" },
    /* T_SEC_MAIN */ { "Основные карты", "Main maps", "Mapas principales" },
    /* T_SEC_OTHER */ { "Тестовые и недоделанные карты", "Test and unfinished maps", "Mapas de prueba y sin terminar" },
    /* T_BACK */ { "Назад", "Back", "Atrás" },
    /* T_RETURN */ { "Вернуться в игру", "Return to game", "Volver al juego" },
    /* T_HINT_APPLY */ { "Карта откроется, когда в игре вы зайдёте проверять оружие (тир в разделе Weaponcrafting). Если игра уже была в меню — сначала переключите любую вкладку и подождите 4 секунды. Игра по сети через «Play» карту не меняет: её выбирает сервер.",
                         "The map opens when you go to test weapons in the game (the shooting range in Weaponcrafting). If the game was already in the menu, switch to any other tab first and wait 4 seconds. Online play via \"Play\" does not use it: the server picks that map.",
                         "El mapa se abre cuando vas a probar armas en el juego (el campo de tiro de Weaponcrafting). Si el juego ya estaba en el menú, cambia antes a otra pestaña y espera 4 segundos. El juego en línea con \"Play\" no lo usa: ese mapa lo elige el servidor." },
    /* T_MAP_CODE */ { "Код карты", "Map code", "Código del mapa" },
    /* T_COPIED */ { "Скопировано", "Copied", "Copiado" },
    /* T_RESET */ { "Сбросить", "Reset", "Restablecer" },
    /* T_START_WARN */ { "Другая карта при запуске может вызвать ошибки. Самая надёжная — Shooting Gallery (стоит по умолчанию).",
                         "A different map at start can cause errors. The safest one is Shooting Gallery (the default).",
                         "Otro mapa al iniciar puede causar errores. El más seguro es Shooting Gallery (predeterminado)." },
    /* T_CUSTOM_HINT */ { "код карты, например fissure_ctf", "map code, e.g. fissure_ctf", "código del mapa, p. ej. fissure_ctf" },
    /* T_TOO_SHORT */ { "Слишком коротко — нужно минимум 4 символа", "Too short — at least 4 characters", "Demasiado corto: mínimo 4 caracteres" },
    /* T_TOO_LONG */ { "Слишком длинно — максимум 50 символов", "Too long — 50 characters max", "Demasiado largo: máximo 50 caracteres" },
    /* T_SETTINGS */ { "Настройки", "Settings", "Ajustes" },
    /* T_SERVER */ { "Адрес сервера", "Server address", "Dirección del servidor" },
    /* T_SERVER_HINT */ { "Применится при следующем запуске игры.", "Applies the next time the game starts.", "Se aplica la próxima vez que se inicie el juego." },
    /* T_DEFAULT */ { "По умолчанию", "Default", "Predeterminado" },
    /* T_SAVE */ { "Сохранить", "Save", "Guardar" },
    /* T_SERVER_INVALID */ { "Неверный адрес — будет использован api.loadout.rip", "Invalid address — api.loadout.rip will be used", "Dirección no válida: se usará api.loadout.rip" },
    /* T_GAME_FOLDER */ { "Папка игры", "Game folder", "Carpeta del juego" },
    /* T_CLOSE */ { "Закрыть", "Close", "Cerrar" },
    /* T_NO_MODE */ { "Без режима", "No mode", "Sin modo" },
    /* T_SENT */ { "Карта отправлена в игру", "Map sent to the game", "Mapa enviado al juego" },
    /* T_PENDING */ { "Карта уйдёт в игру, как только она будет готова", "The map will be sent as soon as the game is ready", "El mapa se enviará en cuanto el juego esté listo" },
    /* T_CHOOSE_TIME */ { "Выберите время суток", "Choose the time of day", "Elige la hora del día" },
    /* T_CHOOSE_MODE */ { "Выберите режим игры", "Choose a game mode", "Elige un modo de juego" },
    /* T_CUSTOM_CODE */ { "своя карта", "custom map", "mapa propio" },
    /* T_TIME */ { "Время", "Time", "Hora" },
    /* T_MODE */ { "Режим", "Mode", "Modo" },
    /* T_MAP */ { "Карта", "Map", "Mapa" },
    /* T_PVE_TIP */ { "Можно играть против ботов", "Playable against bots", "Se puede jugar contra bots" },
    /* T_SAVED */ { "Сохранено", "Saved", "Guardado" },
    /* T_LAUNCH_FAIL */ { "Не удалось запустить игру. Подробности — в файле LoadoutLauncher_Data\\LoadoutLauncher.log в папке игры.",
                          "Could not start the game. Details are in LoadoutLauncher_Data\\LoadoutLauncher.log in the game folder.",
                          "No se pudo iniciar el juego. Detalles en LoadoutLauncher_Data\\LoadoutLauncher.log en la carpeta del juego." },
    /* T_RESTART_NEEDED */ { "Перезапустите игру, чтобы применить новый адрес.", "Restart the game to apply the new address.", "Reinicia el juego para aplicar la nueva dirección." },
};

// ---------------------------------------------------------------- maps
// Exactly the patcher's list (maps that can actually be loaded).
static const char* ALL_MAPS[] = {
    "drillcavern_beta_kc","drillcavern_botwave","drillcavern_cpr","drillcavern_domination","drillcavern_kc","drillcavern_night_botwave",
    "drillcavern_night_cpr","drillcavern_night_domination","drillcavern_night_kc","drillcavern_night_rr","drillcavern_rr","fath_705_botwave",
    "fath_705_cpr","fath_705_domination","fath_705_kc","fath_705_rr","fissure_botwave","fissure_cpr","fissure_ctf","fissure_domination",
    "fissure_kc","fissure_rr","fissurenight_botwave","fissurenight_cpr","fissurenight_ctf","fissurenight_domination","fissurenight_kc",
    "fissurenight_rr","gliese_581_botwave","gliese_581_cpr","gliese_581_ctf","gliese_581_domination","gliese_581_kc","gliese_581_mu",
    "gliese_581_night_botwave","gliese_581_night_cpr","gliese_581_night_ctf","gliese_581_night_domination","gliese_581_night_kc",
    "gliese_581_night_rr","gliese_581_rr","greenroom_ctf","greenroom_tdm","level_three_botwaves","level_three_cpr","level_three_domination",
    "level_three_kc","level_three_rr","locomotiongym","locomotiongym_ctf","locomotiongym_domination","locomotiongym_mashup","projectx_tc",
    "shattered_botwave","shattered_cpr","shattered_ctf","shattered_domination","shattered_kc","shattered_rr","shooting_gallery_solo",
    "spires_botwave","spires_cpr","spires_ctf","spires_domination","spires_kc","spires_rr","sploded_ctf","sploded_kc","test_territorycontrol",
    "thefreezer_botwaves","thefreezer_ctp","thefreezer_kc","thefreezer_tdm","thepit_pj","tower_cpr","tower_ctf","tower_domination","tower_kc",
    "tower_rr","truckstop2_cpr","truckstop2_kc","truckstop2_rr","two_port_ctf","two_port_kc","two_port_rr" };
static const int ALL_MAPS_N = sizeof(ALL_MAPS) / sizeof(ALL_MAPS[0]);

struct BaseMap {
    const char* code;        // base code
    const char* nightPrefix; // prefix for night variant or nullptr
    bool single;             // complete map code, no mode choice
    bool pve;                // "PvE ready" in patcher
    bool main;               // main section or test section
    const char* name;        // in-game name (proper noun)
    const char* img;         // preview resource name or nullptr
    const char* imgNight;
    const char* desc[L_COUNT];
};

static const BaseMap BASE_MAPS[] = {
    { "fath_705", nullptr, false, true, true, "Four Points", "fourpoints", nullptr,
      { "Перевалочная станция со взлётной площадкой", "Supply depot with a landing pad", "Depósito con plataforma de aterrizaje" } },
    { "fissure", "fissurenight", false, true, true, "Fissure", "fissure", "fissure_night",
      { "Шахта среди разломов и каньонов", "Mining site among rifts and canyons", "Mina entre grietas y cañones" } },
    { "level_three", nullptr, false, true, true, "The Brewery", "brewery", nullptr,
      { "Пивоварня в пещере", "Brewery inside a cavern", "Cervecería dentro de una caverna" } },
    { "gliese_581", "gliese_581_night", false, true, true, "Trailer Park", "trailerpark", "trailerpark_night",
      { "Город из сложенных друг на друга трейлеров", "Town of stacked trailers", "Pueblo de tráileres apilados" } },
    { "drillcavern", "drillcavern_night", false, true, true, "Drill Cavern", "drillcavern", "drillcavern_night",
      { "Буровая пещера", "Drilling cavern", "Caverna de perforación" } },
    { "shattered", nullptr, false, true, true, "Shattered", "shattered", nullptr,
      { "Расколотые скалы над пропастью", "Shattered rocks over the abyss", "Rocas rotas sobre el abismo" } },
    { "spires", nullptr, false, true, true, "Spires", "spires", nullptr,
      { "Скальные шпили и дощатые мосты", "Rock spires and plank bridges", "Agujas de roca y puentes de tablas" } },
    { "tower", nullptr, false, false, true, "Comm Tower", "tower", nullptr,
      { "Башня связи", "Communications tower", "Torre de comunicaciones" } },
    { "shooting_gallery_solo", nullptr, true, false, true, "Shooting Gallery", "shootinggallery", nullptr,
      { "Тир для проверки оружия. Самая надёжная карта", "Range for testing weapons. The safest map", "Galería para probar armas. El mapa más seguro" } },

    { "drillcavern_beta_kc", nullptr, true, false, false, "Drill Cavern (Beta)", "drillcavern", nullptr,
      { "Старая бета-версия, режим Deathsnatch", "Old beta version, Deathsnatch mode", "Versión beta antigua, modo Deathsnatch" } },
    { "sploded", nullptr, false, false, false, "Sploded (Alpha)", nullptr, nullptr,
      { "Почти бета, бесцветные деревья", "Almost beta, colorless trees", "Casi beta, árboles sin color" } },
    { "truckstop2", nullptr, false, false, false, "Shipping Yard (Alpha)", "shippingyard", nullptr,
      { "Контейнеры и захваты", "Containers and grippers", "Contenedores y pinzas" } },
    { "two_port", nullptr, false, false, false, "Two Ports (Alpha)", "twoports", nullptr,
      { "По зданию с каждой стороны", "One building on each side", "Un edificio a cada lado" } },
    { "greenroom", nullptr, false, false, false, "Greenroom (Dev)", nullptr, nullptr,
      { "Простая большая зелёная комната", "Simple big green room", "Gran sala verde simple" } },
    { "locomotiongym", nullptr, false, false, false, "Locomotion Gym (Dev)", nullptr, nullptr,
      { "Три мини-карты для проверки режимов", "3 small maps to test game modes", "3 mapas pequeños para probar modos" } },
    { "projectx_tc", nullptr, true, true, false, "Project X (Dev)", nullptr, nullptr,
      { "Большая карта с кучей турелей", "Big map with many turrets", "Mapa grande con muchas torretas" } },
    { "test_territorycontrol", nullptr, true, false, false, "Test (Dev)", nullptr, nullptr,
      { "Крошечная карта с 4 точками", "Tiny map with 4 control points", "Mapa diminuto con 4 puntos" } },
    { "thefreezer", nullptr, false, true, false, "The Freezer (Dev)", nullptr, nullptr,
      { "Маленькая карта с мемом", "Small map with a meme", "Mapa pequeño con un meme" } },
    { "thepit_pj", nullptr, true, false, false, "The Pit (Dev)", nullptr, nullptr,
      { "Летающие взрывающиеся мишени", "Flying targets which explode", "Blancos voladores que explotan" } },
};
static const int BASE_MAPS_N = sizeof(BASE_MAPS) / sizeof(BASE_MAPS[0]);

struct ModeInfo {
    const char* suffix;
    const char* name; // in-game name
    const char* desc[L_COUNT];
};

// Order = display order
static const ModeInfo MODES[] = {
    { "kc", "Deathsnatch", { "Командный бой насмерть", "Team deathmatch", "Combate a muerte por equipos" } },
    { "ctf", "Jackhammer", { "Захватите молот и донесите его к себе", "Grab the hammer and bring it home", "Toma el martillo y llévalo a tu base" } },
    { "rr", "Extraction", { "Собирайте блютоний и бросайте его к себе", "Collect Blutonium and throw it into your base", "Recoge blutonio y lánzalo a tu base" } },
    { "domination", "Domination", { "Удерживайте три точки", "Hold three control points", "Mantén tres puntos de control" } },
    { "cpr", "Blitz", { "Захват точек по очереди", "Capture points one by one", "Captura puntos uno a uno" } },
    { "mu", "Annihilation", { "Командный бой со станциями улучшений", "Team battle with upgrade stations", "Batalla por equipos con estaciones de mejora" } },
    { "botwave", "Hold Your Pole", { "Защита точки от инопланетян-кроадов (против ботов)", "Defend the point from Kroad aliens (vs bots)", "Defiende el punto de los alienígenas Kroad (contra bots)" } },
    { "botwaves", "Hold Your Pole", { "Защита точки от инопланетян-кроадов (против ботов)", "Defend the point from Kroad aliens (vs bots)", "Defiende el punto de los alienígenas Kroad (contra bots)" } },
    { "tdm", "Deathsnatch (Alpha)", { "Ранняя тестовая версия Deathsnatch", "Early test version of Deathsnatch", "Versión de prueba temprana de Deathsnatch" } },
    { "ctp", "Extraction (Alpha)", { "Ранняя тестовая версия Extraction", "Early test version of Extraction", "Versión de prueba temprana de Extraction" } },
    { "mashup", "Annihilation (Alpha)", { "Ранняя тестовая версия Annihilation", "Early test version of Annihilation", "Versión de prueba temprana de Annihilation" } },
    { "tc", "Territory Control", { "Ранняя идея Domination: турели на точках", "Early Domination idea: turrets on points", "Idea temprana de Domination: torretas en puntos" } },
    { "territorycontrol", "Territory Control", { "Одна точка против трёх", "One point against three", "Un punto contra tres" } },
    { "solo", "Solo", { "Боты-мишени, стоят и бегают", "Bot targets, standing and running", "Bots como blancos, quietos y corriendo" } },
    { "pj", "The Pit", { "Цепочки летающих мишеней", "Chains of flying targets", "Cadenas de blancos voladores" } },
    { "", nullptr, { "Просто прогуляться по карте", "Just walk around the map", "Solo recorrer el mapa" } },
};
static const int MODES_N = sizeof(MODES) / sizeof(MODES[0]);
