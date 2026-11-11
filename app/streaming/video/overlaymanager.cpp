#include "overlaymanager.h"
#include "path.h"

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xD0, 0xD0, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;

    // Fixed (not per-message dynamic) width for the hotkey toast and list, so the box
    // doesn't resize as ON/OFF status changes. Set to the longest line across every
    // hotkey toggle message, "W  Swap Command/Control keys: OFF" (measured 569px at
    // 28pt), plus 40px slack - re-measure and update this if the hotkey text ever changes.
    static const int HOTKEY_OVERLAY_FIXED_WIDTH = 609;

    m_Overlays[OverlayType::OverlayHotkeyToast].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayHotkeyToast].fontSize = 28;
    m_Overlays[OverlayType::OverlayHotkeyToast].fixedWidth = HOTKEY_OVERLAY_FIXED_WIDTH;

    m_Overlays[OverlayType::OverlayHotkeyList].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayHotkeyList].fontSize = 28;
    m_Overlays[OverlayType::OverlayHotkeyList].fixedWidth = HOTKEY_OVERLAY_FIXED_WIDTH;

    // Unlike the toast/list above, this one only ever shows one fixed piece of
    // text while visible, so there's no "jumping" concern to size against -
    // tightening to content (fixedWidth left at 0) is fine.
    m_Overlays[OverlayType::OverlayWindowModeHint].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayWindowModeHint].fontSize = 20;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    SDL_utf8strlcpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    {
        QWriteLocker lock(&m_OverlayRendererLock);
        m_Renderer = renderer;
    }

    // notifyOverlayUpdated() is a no-op whenever m_Renderer is still null, with
    // nothing re-triggering it once a renderer does show up - so any overlay
    // enabled before the renderer was registered (for example one enabled up
    // front at session start, rather than toggled live by a hotkey after
    // streaming is already underway) would otherwise never actually render.
    // Replay every currently-enabled overlay now that we have one to catch up.
    if (renderer != nullptr) {
        for (int i = 0; i < OverlayType::OverlayMax; i++) {
            if (m_Overlays[i].enabled) {
                notifyOverlayUpdated((OverlayType)i);
            }
        }
    }
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    QReadLocker lock(&m_OverlayRendererLock);

    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // A fixed width means always render at exactly that width; otherwise size the
    // box to each message's own content (within the usual 1024px wrapping cap).
    bool hasFixedWidth = m_Overlays[type].fixedWidth > 0;
    int renderWrapWidth = hasFixedWidth ? m_Overlays[type].fixedWidth : 1024;

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        m_Overlays[type].enabled ?
            // The _Wrapped variant is required for line breaks to work
            RenderTextOutlinedWrapped(m_Overlays[type].font,
                                      m_Overlays[type].text,
                                      m_Overlays[type].color,
                                      {0, 0, 0, 255},
                                      4,
                                      renderWrapWidth,
                                      !hasFixedWidth)
            : nullptr);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

SDL_Surface* OverlayManager::RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth, bool tightenToContent) {
    if (text == nullptr || text[0] == '\0') {
        return nullptr;
    }

    int oldOutline = TTF_GetFontOutline(font);
    TTF_SetFontOutline(font, outlineWidth);

    // Verify that the string won't require wrapping (which could cause the outline and the text
    // to diverge due to different wrapping positions). While we're at it, remember the widest
    // line's extent (from this same measurement call, so it's consistent with whatever SDL_ttf
    // itself considers each line's width to be) in case the caller wants the box sized closer
    // to the actual content than its maximum wrapWidth.
    //
    // FIXME: We do this rather than just disabling wrapping entirely (wrapWidth = 0) because we
    // need further testing to ensure that all renderers can handle non-NPOT overlay textures.
    int tightWidth = 0;
    for (const QString& line : QString(text).split('\n')) {
        int extent, count;
        if (TTF_MeasureUTF8(font, line.toUtf8(), wrapWidth, &extent, &count) == 0) {
            if (count < line.size()) {
                // If it requires wrapping, render it without the outline
                TTF_SetFontOutline(font, oldOutline);
                return TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
            }

            tightWidth = qMax(tightWidth, extent);
        }
    }

    // When the caller wants a box tightened to this message's own content, leave generous
    // slack: outline rendering can extend a glyph's visual footprint beyond the advance-width
    // metrics used above, and a wrapLength set too close to the measured width risks wrapping
    // the last word early (as happened with a smaller margin here before). Never exceed the
    // caller's original maximum. Otherwise (fixed width), just render at exactly wrapWidth so
    // the box doesn't change size as the message content changes.
    int renderWidth = tightenToContent ? qMin(wrapWidth, tightWidth + 40) : wrapWidth;

    // Draw text twice, but outline is a bit bigger
    auto outlineSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, outlineColor, renderWidth);
    TTF_SetFontOutline(font, 0);
    auto textSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, renderWidth);
    TTF_SetFontOutline(font, oldOutline);

    if (outlineSurface == nullptr || textSurface == nullptr) {
        SDL_FreeSurface(outlineSurface);
        SDL_FreeSurface(textSurface);
        return nullptr;
    }

    // Merge the texts
    SDL_Rect dst = { outlineWidth, outlineWidth, textSurface->w, textSurface->h };
    SDL_BlitSurface(textSurface, nullptr, outlineSurface, &dst);

    SDL_FreeSurface(textSurface);

    // SDL_ttf's wrapped render tight-fits to the actual content regardless of wrapWidth
    // when nothing needs to wrap (verified: passing 1024 vs 4096 for the same short text
    // produces an identical, narrower surface). So for a fixed-width box, passing wrapWidth
    // through alone only sets an upper bound before wrapping kicks in - it does not by
    // itself make a shorter message's surface any wider. Pad it out here instead, left-
    // aligned, transparent on the right, so every message in a fixed-width overlay renders
    // at exactly the same box width.
    if (!tightenToContent && outlineSurface->w < wrapWidth) {
        SDL_Surface* padded = SDL_CreateRGBSurfaceWithFormat(0, wrapWidth, outlineSurface->h, 32, outlineSurface->format->format);
        if (padded != nullptr) {
            SDL_FillRect(padded, nullptr, SDL_MapRGBA(padded->format, 0, 0, 0, 0));

            // Straight copy (not blend) so the source's own alpha is preserved exactly
            SDL_SetSurfaceBlendMode(outlineSurface, SDL_BLENDMODE_NONE);
            SDL_Rect padDst = { 0, 0, outlineSurface->w, outlineSurface->h };
            SDL_BlitSurface(outlineSurface, nullptr, padded, &padDst);

            SDL_FreeSurface(outlineSurface);
            outlineSurface = padded;
        }
    }

    return outlineSurface;
}


