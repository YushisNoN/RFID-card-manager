#  Гайд по ESP-IDF

## Установка (Linux)

    - Для компиляции с использованием ESP-IDF необходимо установить следующие пакеты. Команда зависит от дистрибутива Linux (Ubuntu и Debian):

`
    sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
`

    - Важно: Убедитесь, что у вас установлен Python 3.10 или новее — это минимальная версия, поддерживаемая ESP-IDF.

    - Для сборки приложений для ESP32 вам нужны программные библиотеки, предоставляемые Espressif в репозитории ESP-IDF. Перейдите в каталог установки и клонируйте репозиторий с помощью git clone. Откройте терминал и выполните:

`
mkdir -p ~/esp
cd ~/esp
git clone -b v6.1 --single-branch --recursive https://github.com/espressif/esp-idf.git
`

    - Помимо самого ESP-IDF, необходимо установить инструменты, используемые ESP-IDF: компилятор, отладчик, Python-пакеты и т.д. — для проектов, поддерживающих ESP32:

`
cd ~/esp/esp-idf
./install.sh esp32,esp32s3
`

    - Так же нужную выбрать плату 

`
idf.py set-target esp32s3
`

## Использование 

    - В терминале где вы собираете работать с idf.py выполните :

`
. $HOME/esp/esp-idf/export.sh
`

    - Для добавления зависимостей

        - Из официального рееста:

    `
        idg.py add-dependecny --component=my-component "component-name"
    `

        - Из страндартных библиотек

        Отредкактировать `CMakeListst.txt` добавив `PRIV_REQUIRES/REQUIRES component-name` в idf_component_register

        - Если нужно подтянуть свой файл 

        Отредкактировать `CMakeListst.txt` добавив имя файла сразу после `SRCS`


    - Для точной настройки

`
idf.py menuconfig
`

    - Для сборки проекта выполните из директории  `esp32` 

`
idf.py build
`

    - Для загрузки на плату 

`
idf.py flash -p port monitor 
`

(Для выхода из мониторинга `Ctrl + ]`)

## Личный совет 

Если хотите что-то сделать лучше сначала проверьте оффициальную документацию и реест библиотек/компопнентов скорее всего там уже есть то что вам необходимо
