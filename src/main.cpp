#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/DialogLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <matjson/std.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/binding/SFXBrowser.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/DialogLayer.hpp>
#include <Geode/binding/DialogObject.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <system_error>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

struct MarkerData {
    bool textMode = false;
    int iconIndex = 0;
    int percent = 50;
    std::string text;
};

template <>
struct matjson::Serialize<MarkerData> {
    static Result<MarkerData> fromJson(
        matjson::Value const& value
    ) {
        GEODE_UNWRAP_INTO(
            bool textMode,
            value["textMode"].asBool()
        );

        GEODE_UNWRAP_INTO(
            int iconIndex,
            value["iconIndex"].asInt()
        );

        GEODE_UNWRAP_INTO(
            int percent,
            value["percent"].asInt()
        );

        GEODE_UNWRAP_INTO(
            std::string text,
            value["text"].asString()
        );

        return Ok(
            MarkerData{
                textMode,
                iconIndex,
                percent,
                text
            }
        );
    }

    static matjson::Value toJson(
        MarkerData const& marker
    ) {
        auto value =
            matjson::Value();

        value["textMode"] =
            marker.textMode;

        value["iconIndex"] =
            marker.iconIndex;

        value["percent"] =
            marker.percent;

        value["text"] =
            marker.text;

        return value;
    }
};

void ensureUHDDifficultyFrames() {
    static bool loaded =
        false;

    if (loaded)
        return;

    loaded =
        true;

    auto files =
        CCFileUtils::sharedFileUtils();

    auto cache =
        CCSpriteFrameCache::
            sharedSpriteFrameCache();

    if (
        files &&
        cache &&
        files->isFileExist(
            "GJ_GameSheet03-uhd.plist"
        )
    ) {
        cache->addSpriteFramesWithFile(
            "GJ_GameSheet03-uhd.plist"
        );
    }
}

char const* markerIconFrame(
    int index
) {
    switch (index) {
        case 0:
            return
                "diffIcon_01_btn_001.png";

        case 1:
            return
                "diffIcon_02_btn_001.png";

        case 2:
            return
                "diffIcon_03_btn_001.png";

        case 3:
            return
                "diffIcon_04_btn_001.png";

        case 4:
            return
                "diffIcon_05_btn_001.png";

        case 5:
            return
                "diffIcon_10_btn_001.png";

        case 6:
            return
                "GJ_starsIcon_001.png";

        default:
            return
                "diffIcon_01_btn_001.png";
    }
}

CCSprite* createMarkerIcon(
    int index
) {
    ensureUHDDifficultyFrames();

    return CCSprite::
        createWithSpriteFrameName(
            markerIconFrame(
                index
            )
        );
}

std::string markerSaveKey(
    int levelID
) {
    return fmt::format(
        "markers-{}",
        levelID
    );
}

std::vector<MarkerData> loadMarkers(
    int levelID
) {
    return Mod::get()->
        getSavedValue<
            std::vector<MarkerData>
        >(
            markerSaveKey(
                levelID
            ),
            {}
        );
}

void writeMarkers(
    int levelID,
    std::vector<MarkerData> const& markers
) {
    Mod::get()->
        setSavedValue(
            markerSaveKey(
                levelID
            ),
            markers
        );
}

uint64_t g_markerRevision =
    0;

enum class MarkerSFXSource {
    Off = 0,
    Library = 1,
    Custom = 2
};

MarkerSFXSource getMarkerSFXSource() {
    return static_cast<
        MarkerSFXSource
    >(
        Mod::get()->
            getSavedValue<int>(
                "marker-sfx-source",
                0
            )
    );
}

void playMarkerSFX() {
    auto source =
        getMarkerSFXSource();

    if (
        source ==
        MarkerSFXSource::Off
    )
        return;

    auto engine =
        FMODAudioEngine::
            sharedEngine();

    if (!engine)
        return;

    if (
        source ==
        MarkerSFXSource::Library
    ) {
        int id =
            Mod::get()->
                getSavedValue<int>(
                    "marker-sfx-library-id",
                    0
                );

        if (id <= 0)
            return;

        auto manager =
            MusicDownloadManager::
                sharedState();

        if (!manager)
            return;

        if (
            !manager->
                isResourceSFX(id) &&
            !manager->
                isSFXDownloaded(id)
        ) {
            manager->downloadSFX(
                id
            );

            return;
        }

        auto path =
            manager->
                pathForSFX(id);

        if (path.empty())
            return;

        engine->playEffect(
            path
        );

        return;
    }

    auto path =
        Mod::get()->
            getSavedValue<
                std::string
            >(
                "marker-sfx-custom-path",
                ""
            );

    if (path.empty())
        return;

    std::error_code ec;

    bool exists =
        std::filesystem::exists(
            path,
            ec
        );

    if (
        ec ||
        !exists
    )
        return;

    engine->playEffect(
        path.c_str()
    );
}

class GameplayMarkerLayer :
    public CCNode {
protected:
    CCSprite*
        m_progressBar =
            nullptr;

    struct VisualMarker {
        CCNode*
            root =
                nullptr;

        CCLayerColor*
            line =
                nullptr;

        CCNode*
            display =
                nullptr;

        int percent =
            0;
    };

    std::vector<VisualMarker>
        m_markers;

    void layoutMarkers() {
        if (!m_progressBar)
            return;

        auto size =
            m_progressBar->
                getContentSize();

        auto position =
            m_progressBar->
                getPosition();

        auto anchor =
            m_progressBar->
                getAnchorPoint();

        float baseScale =
            std::abs(
                m_progressBar->
                    getScaleY()
            );

        if (
            baseScale <
            0.001f
        ) {
            baseScale =
                1.f;
        }

        float fullWidth =
            size.width *
            baseScale;

        float barHeight =
            size.height *
            baseScale;

        float left =
            position.x -
            fullWidth *
            anchor.x;

        float centerY =
            position.y +
            barHeight *
            (
                0.5f -
                anchor.y
            );

        float lineHeight =
            barHeight +
            5.f;

        for (
            auto const& marker :
            m_markers
        ) {
            float amount =
                std::clamp(
                    static_cast<float>(
                        marker.percent
                    ) /
                    100.f,
                    0.f,
                    1.f
                );

            float x =
                left +
                fullWidth *
                amount;

            marker.root->
                setPosition({
                    x,
                    centerY
                });

            if (
                marker.line
            ) {
                marker.line->
                    setContentSize({
                        1.f,
                        lineHeight
                    });

                marker.line->
                    setPosition({
                        -0.5f,
                        -lineHeight /
                            2.f
                    });
            }

            if (
                marker.display
            ) {
                marker.display->
                    setPosition({
                        0.f,
                        -(
                            barHeight /
                                2.f +
                            11.f
                        )
                    });
            }
        }
    }

    bool init(
        CCSprite* progressBar,
        std::vector<MarkerData>
            const& markers
    ) {
        if (!CCNode::init())
            return false;

        m_progressBar =
            progressBar;

        for (
            auto const& marker :
            markers
        ) {
            auto root =
                CCNode::create();

            if (!root)
                continue;

            auto line =
                CCLayerColor::
                    create(
                        ccc4(
                            255,
                            255,
                            255,
                            255
                        ),
                        1.f,
                        1.f
                    );

            if (!line)
                continue;

            root->addChild(
                line,
                2
            );

            CCNode*
                display =
                    nullptr;

            if (
                marker.textMode
            ) {
                if (
                    !marker.text.empty()
                ) {
                    auto label =
                        CCLabelBMFont::
                            create(
                                marker
                                    .text
                                    .c_str(),
                                "bigFont.fnt"
                            );

                    if (label) {
                        label->
                            limitLabelWidth(
                                90.f,
                                0.38f,
                                0.16f
                            );

                        display =
                            label;

                        root->addChild(
                            label,
                            3
                        );
                    }
                }
            }
            else {
                auto icon =
                    createMarkerIcon(
                        marker
                            .iconIndex
                    );

                if (icon) {
                    icon->setScale(
                        marker.iconIndex ==
                                6
                            ? 0.44f
                            : 0.58f
                    );

                    display =
                        icon;

                    root->addChild(
                        icon,
                        3
                    );
                }
            }

            addChild(
                root,
                10
            );

            m_markers.push_back({
                root,
                line,
                display,
                std::clamp(
                    marker.percent,
                    0,
                    100
                )
            });
        }

        layoutMarkers();

        scheduleUpdate();

        return true;
    }

    void update(
        float
    ) override {
        if (
            !m_progressBar ||
            !m_progressBar->
                getParent()
        ) {
            setVisible(
                false
            );

            return;
        }

        setVisible(
            m_progressBar->
                isVisible()
        );

        layoutMarkers();
    }

public:
    static GameplayMarkerLayer*
    create(
        CCSprite* progressBar,
        std::vector<MarkerData>
            const& markers
    ) {
        auto ret =
            new GameplayMarkerLayer();

        if (
            ret &&
            ret->init(
                progressBar,
                markers
            )
        ) {
            ret->autorelease();

            return ret;
        }

        delete ret;

        return nullptr;
    }
};

void refreshGameplayMarkers(
    PlayLayer* play
) {
    if (
        !play ||
        !play->m_level ||
        !play->m_progressBar
    )
        return;

    auto parent =
        play->
            m_progressBar->
            getParent();

    if (!parent)
        return;

    if (
        auto old =
            parent->getChildByID(
                "gameplay-markers"_spr
            )
    ) {
        old->
            removeFromParentAndCleanup(
                true
            );
    }

    int levelID =
        static_cast<int>(
            play->
                m_level->
                m_levelID
        );

    auto markers =
        loadMarkers(
            levelID
        );

    if (
        markers.empty()
    )
        return;

    auto layer =
        GameplayMarkerLayer::
            create(
                play->
                    m_progressBar,
                markers
            );

    if (!layer)
        return;

    layer->setID(
        "gameplay-markers"_spr
    );

    layer->setPosition({
        0.f,
        0.f
    });

    parent->addChild(
        layer,
        play->
            m_progressBar->
            getZOrder() +
            100
    );
}

enum class RowMode {
    Choice,
    Icon,
    Text
};

class MarkerRow :
    public CCNode {
protected:
    RowMode m_mode =
        RowMode::Choice;

    MarkerData m_data;

    CCMenu*
        m_menu =
            nullptr;

    CCSprite*
        m_iconDisplay =
            nullptr;

    CCMenuItemSpriteExtra*
        m_iconButton =
            nullptr;

    TextInput*
        m_textInput =
            nullptr;

    TextInput*
        m_percentInput =
            nullptr;

    std::function<
        void(MarkerRow*)
    > m_onDelete;

    std::function<
        void(MarkerRow*)
    > m_onIconPicker;

    std::function<
        void()
    > m_onChanged;

    void resetPointers() {
        m_menu =
            nullptr;

        m_iconDisplay =
            nullptr;

        m_iconButton =
            nullptr;

        m_textInput =
            nullptr;

        m_percentInput =
            nullptr;
    }

    void clearRow() {
        removeAllChildrenWithCleanup(
            true
        );

        resetPointers();
    }

    void addBackground() {
        auto bg =
            CCScale9Sprite::
                create(
                    "square02b_small.png"
                );

        if (!bg)
            return;

        bg->setContentSize({
            365.f,
            38.f
        });

        bg->setPosition({
            182.5f,
            21.f
        });

        bg->setColor(
            ccc3(
                20,
                20,
                20
            )
        );

        bg->setOpacity(
            128
        );

        addChild(
            bg
        );
    }

    void buildChoice() {
        clearRow();

        addBackground();

        m_menu =
            CCMenu::create();

        if (!m_menu)
            return;

        m_menu->setPosition({
            0.f,
            0.f
        });

        addChild(
            m_menu
        );

        auto iconSprite =
            ButtonSprite::create(
                "ICON"
            );

        if (iconSprite) {
            iconSprite->
                setScale(
                    0.62f
                );

            auto iconButton =
                CCMenuItemExt::
                    createSpriteExtra(
                        iconSprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            configure(
                                false
                            );
                        }
                    );

            iconButton->
                setPosition({
                    140.f,
                    21.f
                });

            m_menu->addChild(
                iconButton
            );
        }

        auto textSprite =
            ButtonSprite::create(
                "TEXT"
            );

        if (textSprite) {
            textSprite->
                setScale(
                    0.62f
                );

            auto textButton =
                CCMenuItemExt::
                    createSpriteExtra(
                        textSprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            configure(
                                true
                            );
                        }
                    );

            textButton->
                setPosition({
                    225.f,
                    21.f
                });

            m_menu->addChild(
                textButton
            );
        }
    }

    void buildConfigured() {
        clearRow();

        addBackground();

        m_menu =
            CCMenu::create();

        if (!m_menu)
            return;

        m_menu->setPosition({
            0.f,
            0.f
        });

        addChild(
            m_menu
        );

        if (
            m_mode ==
            RowMode::Icon
        ) {
            m_iconDisplay =
                createMarkerIcon(
                    m_data.iconIndex
                );

            if (
                m_iconDisplay
            ) {
                m_iconDisplay->
                    setScale(
                        m_data.iconIndex ==
                                6
                            ? 0.66f
                            : 0.72f
                    );

                auto circle =
                    CircleButtonSprite::
                        create(
                            m_iconDisplay,
                            CircleBaseColor::
                                Green,
                            CircleBaseSize::
                                Small
                        );

                if (circle) {
                    circle->setScale(
                        0.75f
                    );

                    m_iconButton =
                        CCMenuItemExt::
                            createSpriteExtra(
                                circle,
                                [this](
                                    CCMenuItemSpriteExtra*
                                ) {
                                    if (
                                        m_onIconPicker
                                    ) {
                                        m_onIconPicker(
                                            this
                                        );
                                    }
                                }
                            );

                    m_iconButton->
                        setPosition({
                            90.f,
                            21.f
                        });

                    m_menu->addChild(
                        m_iconButton
                    );
                }
            }
        }

        if (
            m_mode ==
            RowMode::Text
        ) {
            m_textInput =
                TextInput::create(
                    175.f,
                    "Message",
                    "bigFont.fnt"
                );

            if (
                m_textInput
            ) {
                m_textInput->
                    setPosition({
                        105.f,
                        21.f
                    });

                m_textInput->
                    setMaxCharCount(
                        32
                    );

                m_textInput->
                    setString(
                        m_data.text,
                        false
                    );

                m_textInput->
                    setCallback(
                        [this](
                            std::string
                                const& text
                        ) {
                            m_data.text =
                                text;

                            if (
                                m_onChanged
                            ) {
                                m_onChanged();
                            }
                        }
                    );

                addChild(
                    m_textInput
                );
            }
        }

        m_percentInput =
            TextInput::create(
                52.f,
                "50",
                "bigFont.fnt"
            );

        if (
            m_percentInput
        ) {
            m_percentInput->
                setPosition({
                    235.f,
                    21.f
                });

            m_percentInput->
                setFilter(
                    "0123456789"
                );

            m_percentInput->
                setMaxCharCount(
                    3
                );

            m_percentInput->
                setString(
                    fmt::format(
                        "{}",
                        m_data.percent
                    ),
                    false
                );

            m_percentInput->
                setCallback(
                    [this](
                        std::string
                            const& text
                    ) {
                        if (
                            text.empty()
                        ) {
                            m_data.percent =
                                0;
                        }
                        else {
                            int value =
                                std::atoi(
                                    text.c_str()
                                );

                            int clamped =
                                std::clamp(
                                    value,
                                    0,
                                    100
                                );

                            m_data.percent =
                                clamped;

                            if (
                                value !=
                                clamped
                            ) {
                                m_percentInput->
                                    setString(
                                        fmt::format(
                                            "{}",
                                            clamped
                                        ),
                                        false
                                    );
                            }
                        }

                        if (
                            m_onChanged
                        ) {
                            m_onChanged();
                        }
                    }
                );

            addChild(
                m_percentInput
            );
        }

        auto percentLabel =
            CCLabelBMFont::
                create(
                    "%",
                    "bigFont.fnt"
                );

        if (
            percentLabel
        ) {
            percentLabel->
                setScale(
                    0.45f
                );

            percentLabel->
                setPosition({
                    273.f,
                    21.f
                });

            addChild(
                percentLabel
            );
        }

        auto trashSprite =
            CCSprite::
                createWithSpriteFrameName(
                    "GJ_deleteBtn_001.png"
                );

        if (
            trashSprite
        ) {
            trashSprite->
                setScale(
                    0.72f
                );

            auto trash =
                CCMenuItemExt::
                    createSpriteExtra(
                        trashSprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            if (
                                m_onDelete
                            ) {
                                m_onDelete(
                                    this
                                );
                            }
                        }
                    );

            trash->
                setPosition({
                    335.f,
                    21.f
                });

            m_menu->addChild(
                trash
            );
        }
    }

    bool init(
        RowMode mode,
        MarkerData const& data,
        std::function<
            void(MarkerRow*)
        > onDelete,
        std::function<
            void(MarkerRow*)
        > onIconPicker,
        std::function<
            void()
        > onChanged
    ) {
        if (!CCNode::init())
            return false;

        m_mode =
            mode;

        m_data =
            data;

        m_data.iconIndex =
            std::clamp(
                m_data.iconIndex,
                0,
                6
            );

        m_data.percent =
            std::clamp(
                m_data.percent,
                0,
                100
            );

        m_onDelete =
            std::move(
                onDelete
            );

        m_onIconPicker =
            std::move(
                onIconPicker
            );

        m_onChanged =
            std::move(
                onChanged
            );

        setContentSize({
            365.f,
            42.f
        });

        if (
            m_mode ==
            RowMode::Choice
        ) {
            buildChoice();
        }
        else {
            buildConfigured();
        }

        return true;
    }

public:
    static MarkerRow*
    create(
        RowMode mode,
        MarkerData const& data,
        std::function<
            void(MarkerRow*)
        > onDelete,
        std::function<
            void(MarkerRow*)
        > onIconPicker,
        std::function<
            void()
        > onChanged
    ) {
        auto ret =
            new MarkerRow();

        if (
            ret &&
            ret->init(
                mode,
                data,
                std::move(
                    onDelete
                ),
                std::move(
                    onIconPicker
                ),
                std::move(
                    onChanged
                )
            )
        ) {
            ret->autorelease();

            return ret;
        }

        delete ret;

        return nullptr;
    }

    void configure(
        bool textMode
    ) {
        m_data =
            MarkerData{};

        m_data.textMode =
            textMode;

        m_data.percent =
            50;

        m_mode =
            textMode
                ? RowMode::Text
                : RowMode::Icon;

        buildConfigured();

        if (
            m_onChanged
        ) {
            m_onChanged();
        }
    }

    void setIconIndex(
        int index
    ) {
        if (
            m_mode !=
            RowMode::Icon
        )
            return;

        m_data.iconIndex =
            std::clamp(
                index,
                0,
                6
            );

        if (
            m_iconDisplay
        ) {
            ensureUHDDifficultyFrames();

            auto frame =
                CCSpriteFrameCache::
                    sharedSpriteFrameCache()
                    ->
                    spriteFrameByName(
                        markerIconFrame(
                            m_data
                                .iconIndex
                        )
                    );

            if (frame) {
                m_iconDisplay->
                    setDisplayFrame(
                        frame
                    );
            }

            m_iconDisplay->
                setScale(
                    m_data.iconIndex ==
                            6
                        ? 0.66f
                        : 0.72f
                );
        }

        if (
            m_onChanged
        ) {
            m_onChanged();
        }
    }

    bool isChoice() const {
        return
            m_mode ==
            RowMode::Choice;
    }

    MarkerData getData() const {
        return m_data;
    }
};

class MarkerSFXSettingsPopup :
    public Popup,
    public SFXBrowserDelegate {
protected:
    CCLabelBMFont*
        m_status =
            nullptr;

    void refreshStatus() {
        if (!m_status)
            return;

        auto source =
            getMarkerSFXSource();

        if (
            source ==
            MarkerSFXSource::Off
        ) {
            m_status->
                setString(
                    "SFX: OFF"
                );

            return;
        }

        if (
            source ==
            MarkerSFXSource::Library
        ) {
            int id =
                Mod::get()->
                    getSavedValue<int>(
                        "marker-sfx-library-id",
                        0
                    );

            auto text =
                fmt::format(
                    "GD SFX: {}",
                    id
                );

            m_status->
                setString(
                    text.c_str()
                );

            return;
        }

        auto path =
            Mod::get()->
                getSavedValue<
                    std::string
                >(
                    "marker-sfx-custom-path",
                    ""
                );

        if (
            path.empty()
        ) {
            m_status->
                setString(
                    "Custom SFX: None"
                );

            return;
        }

        auto name =
            std::filesystem::
                path(
                    path
                )
                .filename()
                .string();

        auto text =
            fmt::format(
                "Custom: {}",
                name
            );

        m_status->
            setString(
                text.c_str()
            );
    }

    void sfxBrowserClosed(
        SFXBrowser* browser
    ) override {
        if (!browser)
            return;

        int id =
            browser->m_sfxID;

        if (id <= 0)
            return;

        Mod::get()->
            setSavedValue(
                "marker-sfx-library-id",
                id
            );

        Mod::get()->
            setSavedValue(
                "marker-sfx-source",
                static_cast<int>(
                    MarkerSFXSource::
                        Library
                )
            );

        auto manager =
            MusicDownloadManager::
                sharedState();

        if (
            manager &&
            !manager->
                isResourceSFX(id) &&
            !manager->
                isSFXDownloaded(id)
        ) {
            manager->downloadSFX(
                id
            );
        }

        refreshStatus();
    }

    void chooseCustomSFX() {
        retain();

        async::spawn(
            file::pick(
                file::PickMode::
                    OpenFile,
                {}
            ),
            [this](
                Result<
                    std::optional<
                        std::filesystem::
                            path
                    >
                > result
            ) {
                if (
                    result.isOk()
                ) {
                    auto selected =
                        result.unwrap();

                    if (
                        selected
                    ) {
                        Mod::get()->
                            setSavedValue(
                                "marker-sfx-custom-path",
                                selected->
                                    string()
                            );

                        Mod::get()->
                            setSavedValue(
                                "marker-sfx-source",
                                static_cast<int>(
                                    MarkerSFXSource::
                                        Custom
                                )
                            );

                        if (
                            getParent()
                        ) {
                            refreshStatus();
                        }
                    }
                }

                release();
            }
        );
    }

    bool init() {
        if (
            !Popup::init(
                340.f,
                200.f
            )
        )
            return false;

        setTitle(
            "Marker SFX",
            "goldFont.fnt",
            0.8f
        );

        m_status =
            CCLabelBMFont::
                create(
                    "",
                    "bigFont.fnt"
                );

        if (m_status) {
            m_status->
                setScale(
                    0.4f
                );

            m_status->
                setPosition({
                    m_size.width /
                        2.f,
                    142.f
                });

            m_mainLayer->
                addChild(
                    m_status
                );
        }

        auto offSprite =
            ButtonSprite::
                create(
                    "OFF"
                );

        if (offSprite) {
            offSprite->
                setScale(
                    0.65f
                );

            auto off =
                CCMenuItemExt::
                    createSpriteExtra(
                        offSprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            Mod::get()->
                                setSavedValue(
                                    "marker-sfx-source",
                                    static_cast<int>(
                                        MarkerSFXSource::
                                            Off
                                    )
                                );

                            refreshStatus();
                        }
                    );

            off->setPosition({
                62.f,
                100.f
            });

            m_buttonMenu->
                addChild(
                    off
                );
        }

        auto librarySprite =
            ButtonSprite::
                create(
                    "GD LIBRARY"
                );

        if (
            librarySprite
        ) {
            librarySprite->
                setScale(
                    0.65f
                );

            auto library =
                CCMenuItemExt::
                    createSpriteExtra(
                        librarySprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            int id =
                                Mod::get()->
                                    getSavedValue<int>(
                                        "marker-sfx-library-id",
                                        0
                                    );

                            auto browser =
                                SFXBrowser::
                                    create(
                                        id
                                    );

                            if (!browser)
                                return;

                            browser->
                                m_delegate =
                                    this;

                            browser->show();
                        }
                    );

            library->
                setPosition({
                    170.f,
                    100.f
                });

            m_buttonMenu->
                addChild(
                    library
                );
        }

        auto customSprite =
            ButtonSprite::
                create(
                    "CUSTOM"
                );

        if (
            customSprite
        ) {
            customSprite->
                setScale(
                    0.65f
                );

            auto custom =
                CCMenuItemExt::
                    createSpriteExtra(
                        customSprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            chooseCustomSFX();
                        }
                    );

            custom->
                setPosition({
                    278.f,
                    100.f
                });

            m_buttonMenu->
                addChild(
                    custom
                );
        }

        auto testSprite =
            ButtonSprite::
                create(
                    "TEST"
                );

        if (testSprite) {
            testSprite->
                setScale(
                    0.65f
                );

            auto test =
                CCMenuItemExt::
                    createSpriteExtra(
                        testSprite,
                        [](
                            CCMenuItemSpriteExtra*
                        ) {
                            playMarkerSFX();
                        }
                    );

            test->setPosition({
                m_size.width /
                    2.f,
                55.f
            });

            m_buttonMenu->
                addChild(
                    test
                );
        }

        refreshStatus();

        return true;
    }

public:
    static
    MarkerSFXSettingsPopup*
    create() {
        auto ret =
            new MarkerSFXSettingsPopup();

        if (
            ret &&
            ret->init()
        ) {
            ret->autorelease();

            return ret;
        }

        delete ret;

        return nullptr;
    }
};

char const*
deathMarkerPortraitFrame(
    DialogObject* object
) {
    if (!object)
        return nullptr;

    std::string character =
        object->m_character;

    std::string text =
        object->m_text;

    if (
        character ==
            "Easy" &&
        text.find(
            "SPEAK FOR YOURSELF"
        ) !=
            std::string::npos
    ) {
        return
            "diffIcon_05_btn_001.png";
    }

    if (
        character ==
            "Easy" &&
        text.find(
            "I HAVE SAID TOO MUCH"
        ) !=
            std::string::npos
    ) {
        return
            "diffIcon_05_btn_001.png";
    }

    if (
        character ==
            "Easy" &&
        text.find(
            "<d010>.<d010>.<d010>."
        ) !=
            std::string::npos
    ) {
        return
            "diffIcon_02_btn_001.png";
    }

    if (
        character ==
        "Easy Demon"
    ) {
        return
            "diffIcon_07_btn_001.png";
    }

    if (
        character ==
        "Auto"
    ) {
        return
            "diffIcon_auto_btn_001.png";
    }

    if (
        character ==
        "N/A"
    ) {
        return
            "diffIcon_00_btn_001.png";
    }

    if (
        character ==
        "Easy"
    ) {
        return
            "diffIcon_01_btn_001.png";
    }

    return nullptr;
}

void applyDeathMarkerPortrait(
    DialogLayer* dialog,
    DialogObject* object
) {
    if (
        !dialog ||
        !object ||
        !dialog->
            m_characterSprite
    )
        return;

    ensureUHDDifficultyFrames();

    auto frameName =
        deathMarkerPortraitFrame(
            object
        );

    if (!frameName)
        return;

    auto frame =
        CCSpriteFrameCache::
            sharedSpriteFrameCache()
            ->
            spriteFrameByName(
                frameName
            );

    if (!frame)
        return;

    auto sprite =
        dialog->
            m_characterSprite;

    sprite->
        setDisplayFrame(
            frame
        );

    sprite->setColor(
        ccWHITE
    );

    sprite->setOpacity(
        255
    );

    sprite->setRotation(
        0.f
    );

    sprite->setFlipX(
        false
    );

    sprite->setFlipY(
        false
    );

    auto size =
        sprite->
            getContentSize();

    float largest =
        std::max(
            size.width,
            size.height
        );

    if (
        largest >
        0.f
    ) {
        sprite->setScale(
            74.f /
            largest
        );
    }
}

class $modify(
    PercentMarkersDialogLayer,
    DialogLayer
) {
    void onEnter() {
        DialogLayer::
            onEnter();

        if (
            getID() !=
            "death-markers-dialog"_spr
        )
            return;

        if (
            !m_characterSprite
        )
            return;

        ensureUHDDifficultyFrames();

        auto frame =
            CCSpriteFrameCache::
                sharedSpriteFrameCache()
                ->
                spriteFrameByName(
                    "diffIcon_00_btn_001.png"
                );

        if (!frame)
            return;

        m_characterSprite->
            setDisplayFrame(
                frame
            );

        m_characterSprite->
            setColor(
                ccWHITE
            );

        m_characterSprite->
            setOpacity(
                255
            );

        m_characterSprite->
            setRotation(
                0.f
            );

        m_characterSprite->
            setFlipX(
                false
            );

        m_characterSprite->
            setFlipY(
                false
            );

        auto size =
            m_characterSprite->
                getContentSize();

        float largest =
            std::max(
                size.width,
                size.height
            );

        if (
            largest >
            0.f
        ) {
            m_characterSprite->
                setScale(
                    74.f /
                    largest
                );
        }
    }

    void displayDialogObject(
        DialogObject* object
    ) {
        DialogLayer::
            displayDialogObject(
                object
            );

        if (
            getID() !=
            "death-markers-dialog"_spr
        )
            return;

        applyDeathMarkerPortrait(
            this,
            object
        );
    }
};

class MarkersPopup :
    public Popup {
protected:
    int m_levelID =
        0;

    bool m_dirty =
        false;

    ScrollLayer*
        m_scroll =
            nullptr;

    CCNode*
        m_iconPicker =
            nullptr;

    std::vector<
        MarkerRow*
    > m_rows;

    void showDeathMarkersDialog() {
        int backgroundColor =
            2;

        auto dialogLines =
            CCArray::create();

        dialogLines->addObject(
            DialogObject::create(
                "N/A",
                "<cj>Death Markers</c> is delayed, but its in the <co>works!</c>",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy",
                "I blame <cy>ItsYVoid</c>.",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy Demon",
                "Who else could we blame? We're not even real.",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy",
                "<s260>SPEAK FOR YOURSELF</s> I'M REAL !",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy Demon",
                "Yeah, a real pain the...",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy",
                "You're just mad because you're not popular.",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "N/A",
                "You're <cy>popular?</c>",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy",
                "<cg>I HAVE SAID TOO MUCH QUICKLY GO TO THE <s260>PROGRESS BAR</s></c>",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Auto",
                "You don't have a progress bar<d010>.<d010>.<d010>.",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        dialogLines->addObject(
            DialogObject::create(
                "Easy",
                "<d010>.<d010>.<d010>.",
                2,
                1.0f,
                false,
                ccWHITE
            )
        );

        auto dialog =
            DialogLayer::
                createWithObjects(
                    dialogLines,
                    backgroundColor
                );

        if (!dialog)
            return;

        dialog->setID(
            "death-markers-dialog"_spr
        );

        dialog->
            updateChatPlacement(
                DialogChatPlacement::
                    Center
            );

        dialog->
            animateInRandomSide();

        dialog->
            addToMainScene();
    }

    void closeIconPicker() {
        if (!m_iconPicker)
            return;

        m_iconPicker->
            removeFromParentAndCleanup(
                true
            );

        m_iconPicker =
            nullptr;
    }

    void refreshRows() {
        float height =
            std::max(
                145.f,
                static_cast<float>(
                    m_rows.size()
                ) *
                46.f
            );

        m_scroll->
            m_contentLayer->
            setContentSize({
                390.f,
                height
            });

        for (
            size_t i = 0;
            i < m_rows.size();
            i++
        ) {
            m_rows[i]->
                setPosition({
                    12.f,
                    height -
                        44.f -
                        static_cast<float>(
                            i
                        ) *
                        46.f
                });
        }
    }

    std::vector<MarkerData>
    collectRows() const {
        std::vector<MarkerData>
            markers;

        markers.reserve(
            m_rows.size()
        );

        for (
            auto row :
            m_rows
        ) {
            if (
                row &&
                !row->isChoice()
            ) {
                markers.push_back(
                    row->getData()
                );
            }
        }

        return markers;
    }

    void markDirty() {
        m_dirty =
            true;
    }

    void commitChanges() {
        if (!m_dirty)
            return;

        writeMarkers(
            m_levelID,
            collectRows()
        );

        g_markerRevision++;

        if (
            auto play =
                PlayLayer::get()
        ) {
            refreshGameplayMarkers(
                play
            );
        }

        m_dirty =
            false;
    }

    void deleteRow(
        MarkerRow* row
    ) {
        closeIconPicker();

        auto it =
            std::find(
                m_rows.begin(),
                m_rows.end(),
                row
            );

        if (
            it ==
            m_rows.end()
        )
            return;

        bool configured =
            !row->isChoice();

        m_rows.erase(
            it
        );

        row->
            removeFromParentAndCleanup(
                true
            );

        refreshRows();

        if (
            configured
        ) {
            markDirty();
        }
    }

    void showIconPicker(
        MarkerRow* row
    ) {
        if (!row)
            return;

        closeIconPicker();

        auto world =
            row->
                convertToWorldSpace({
                    90.f,
                    21.f
                });

        auto local =
            m_mainLayer->
                convertToNodeSpace(
                    world
                );

        float x =
            std::clamp(
                local.x,
                135.f,
                m_size.width -
                    135.f
            );

        float y =
            std::clamp(
                local.y -
                    48.f,
                40.f,
                m_size.height -
                    40.f
            );

        m_iconPicker =
            CCNode::create();

        if (!m_iconPicker)
            return;

        m_iconPicker->
            setContentSize({
                260.f,
                46.f
            });

        m_iconPicker->
            setPosition({
                x - 130.f,
                y - 23.f
            });

        m_mainLayer->
            addChild(
                m_iconPicker,
                100
            );

        auto bg =
            CCScale9Sprite::
                create(
                    "square02b_small.png"
                );

        if (bg) {
            bg->setContentSize({
                260.f,
                46.f
            });

            bg->setPosition({
                130.f,
                23.f
            });

            bg->setColor(
                ccc3(
                    20,
                    20,
                    20
                )
            );

            bg->setOpacity(
                210
            );

            m_iconPicker->
                addChild(
                    bg
                );
        }

        auto menu =
            CCMenu::create();

        if (!menu)
            return;

        menu->setPosition({
            0.f,
            0.f
        });

        m_iconPicker->
            addChild(
                menu
            );

        for (
            int i = 0;
            i < 7;
            i++
        ) {
            auto icon =
                createMarkerIcon(
                    i
                );

            if (!icon)
                continue;

            icon->setScale(
                i == 6
                    ? 0.60f
                    : 0.68f
            );

            auto button =
                CCMenuItemExt::
                    createSpriteExtra(
                        icon,
                        [this, row, i](
                            CCMenuItemSpriteExtra*
                        ) {
                            row->
                                setIconIndex(
                                    i
                                );

                            closeIconPicker();
                        }
                    );

            button->
                setPosition({
                    20.f +
                        static_cast<float>(
                            i
                        ) *
                        36.f,
                    23.f
                });

            menu->addChild(
                button
            );
        }
    }

    void addChoiceRow() {
        closeIconPicker();

        MarkerData data;

        auto row =
            MarkerRow::create(
                RowMode::Choice,
                data,
                [this](
                    MarkerRow* row
                ) {
                    deleteRow(
                        row
                    );
                },
                [this](
                    MarkerRow* row
                ) {
                    showIconPicker(
                        row
                    );
                },
                [this]() {
                    markDirty();
                }
            );

        if (!row)
            return;

        m_scroll->
            m_contentLayer->
            addChild(
                row
            );

        m_rows.push_back(
            row
        );

        refreshRows();

        m_scroll->
            scrollToTop();
    }

    void addSavedRow(
        MarkerData const& data
    ) {
        auto mode =
            data.textMode
                ? RowMode::Text
                : RowMode::Icon;

        auto row =
            MarkerRow::create(
                mode,
                data,
                [this](
                    MarkerRow* row
                ) {
                    deleteRow(
                        row
                    );
                },
                [this](
                    MarkerRow* row
                ) {
                    showIconPicker(
                        row
                    );
                },
                [this]() {
                    markDirty();
                }
            );

        if (!row)
            return;

        m_scroll->
            m_contentLayer->
            addChild(
                row
            );

        m_rows.push_back(
            row
        );
    }

    bool init(
        int levelID
    ) {
        if (
            !Popup::init(
                440.f,
                245.f
            )
        )
            return false;

        m_levelID =
            levelID;

        setTitle(
            "Percent Markers",
            "goldFont.fnt",
            0.8f
        );

        m_scroll =
            ScrollLayer::
                create(
                    CCSize{
                        390.f,
                        145.f
                    },
                    true,
                    true
                );

        if (!m_scroll)
            return false;

        m_scroll->
            setPosition({
                25.f,
                63.f
            });

        m_mainLayer->
            addChild(
                m_scroll
            );

        auto plusSprite =
            CCSprite::
                createWithSpriteFrameName(
                    "GJ_plusBtn_001.png"
                );

        if (!plusSprite)
            return false;

        plusSprite->
            setScale(
                0.72f
            );

        auto plus =
            CCMenuItemExt::
                createSpriteExtra(
                    plusSprite,
                    [this](
                        CCMenuItemSpriteExtra*
                    ) {
                        addChoiceRow();
                    }
                );

        plus->
            setPosition({
                m_size.width /
                    2.f,
                35.f
            });

        m_buttonMenu->
            addChild(
                plus
            );

        auto settingsSprite =
            CCSprite::
                createWithSpriteFrameName(
                    "GJ_optionsBtn_001.png"
                );

        if (
            settingsSprite
        ) {
            settingsSprite->
                setScale(
                    0.45f
                );

            auto settings =
                CCMenuItemExt::
                    createSpriteExtra(
                        settingsSprite,
                        [](
                            CCMenuItemSpriteExtra*
                        ) {
                            auto popup =
                                MarkerSFXSettingsPopup::
                                    create();

                            if (popup) {
                                popup->show();
                            }
                        }
                    );

            settings->
                setPosition({
                    38.f,
                    35.f
                });

            m_buttonMenu->
                addChild(
                    settings
                );
        }

        auto deathSprite =
            ButtonSprite::create(
                "Death Markers"
            );

        if (
            deathSprite
        ) {
            deathSprite->
                setScale(
                    0.50f
                );

            auto deathButton =
                CCMenuItemExt::
                    createSpriteExtra(
                        deathSprite,
                        [this](
                            CCMenuItemSpriteExtra*
                        ) {
                            showDeathMarkersDialog();
                        }
                    );

            deathButton->
                setPosition({
                    m_size.width -
                        72.f,
                    35.f
                });

            m_buttonMenu->
                addChild(
                    deathButton
                );
        }

        auto markers =
            loadMarkers(
                m_levelID
            );

        for (
            auto const& marker :
            markers
        ) {
            addSavedRow(
                marker
            );
        }

        refreshRows();

        if (
            !m_rows.empty()
        ) {
            m_scroll->
                scrollToTop();
        }

        m_dirty =
            false;

        return true;
    }

    void onClose(
        CCObject* sender
    ) override {
        commitChanges();

        Popup::onClose(
            sender
        );
    }

    void keyBackClicked()
        override {
        commitChanges();

        Popup::
            keyBackClicked();
    }

public:
    static MarkersPopup*
    create(
        int levelID
    ) {
        auto ret =
            new MarkersPopup();

        if (
            ret &&
            ret->init(
                levelID
            )
        ) {
            ret->autorelease();

            return ret;
        }

        delete ret;

        return nullptr;
    }
};

class $modify(
    PercentMarkersPlayLayer,
    PlayLayer
) {
    struct Fields {
        int lastPercent =
            -1;

        uint64_t markerRevision =
            0;

        std::unordered_set<
            size_t
        > triggered;

        std::vector<
            MarkerData
        > markers;
    };

    void reloadMarkerData() {
        if (!m_level)
            return;

        int levelID =
            static_cast<int>(
                m_level->
                    m_levelID
            );

        m_fields->
            markers =
                loadMarkers(
                    levelID
                );

        m_fields->
            triggered
            .clear();

        m_fields->
            markerRevision =
                g_markerRevision;
    }

    bool init(
        GJGameLevel* level,
        bool useReplay,
        bool dontCreateObjects
    ) {
        if (
            !PlayLayer::init(
                level,
                useReplay,
                dontCreateObjects
            )
        )
            return false;

        reloadMarkerData();

        m_fields->
            lastPercent =
                -1;

        refreshGameplayMarkers(
            this
        );

        return true;
    }

    void postUpdate(
        float dt
    ) {
        PlayLayer::
            postUpdate(
                dt
            );

        int current =
            std::clamp(
                getCurrentPercentInt(),
                0,
                100
            );

        if (
            m_fields->
                markerRevision !=
            g_markerRevision
        ) {
            reloadMarkerData();

            m_fields->
                lastPercent =
                    current;

            return;
        }

        if (
            m_fields->
                lastPercent >=
                    0 &&
            current <
            m_fields->
                lastPercent
        ) {
            m_fields->
                triggered
                .clear();

            m_fields->
                lastPercent =
                    current;

            return;
        }

        bool crossedMarker =
            false;

        for (
            size_t i = 0;
            i <
            m_fields->
                markers
                .size();
            i++
        ) {
            auto const& marker =
                m_fields->
                    markers[i];

            int target =
                std::clamp(
                    marker.percent,
                    0,
                    100
                );

            if (
                !m_fields->
                    triggered
                    .contains(
                        i
                    ) &&
                m_fields->
                    lastPercent <
                    target &&
                current >=
                    target
            ) {
                m_fields->
                    triggered
                    .insert(
                        i
                    );

                crossedMarker =
                    true;
            }
        }

        if (
            crossedMarker
        ) {
            playMarkerSFX();
        }

        m_fields->
            lastPercent =
                current;
    }
};

class $modify(
    PercentMarkersPauseLayer,
    PauseLayer
) {
    void customSetup() {
        PauseLayer::
            customSetup();

        NodeIDs::
            provideFor(
                this
            );

        auto menu =
            getChildByID(
                "right-button-menu"
            );

        if (!menu)
            return;

        if (
            menu->getChildByID(
                "percent-markers-button"_spr
            )
        )
            return;

        auto icon =
            createMarkerIcon(
                0
            );

        if (!icon)
            return;

        icon->setScale(
            0.64f
        );

        auto circle =
            CircleButtonSprite::
                create(
                    icon,
                    CircleBaseColor::
                        Green,
                    CircleBaseSize::
                        Small
                );

        if (!circle)
            return;

        auto button =
            CCMenuItemExt::
                createSpriteExtra(
                    circle,
                    [](
                        CCMenuItemSpriteExtra*
                    ) {
                        auto play =
                            PlayLayer::
                                get();

                        if (
                            !play ||
                            !play->
                                m_level
                        )
                            return;

                        int levelID =
                            static_cast<int>(
                                play->
                                    m_level->
                                    m_levelID
                            );

                        auto popup =
                            MarkersPopup::
                                create(
                                    levelID
                                );

                        if (popup) {
                            popup->
                                show();
                        }
                    }
                );

        button->setID(
            "percent-markers-button"_spr
        );

        menu->addChild(
            button
        );

        menu->updateLayout();
    }
};
