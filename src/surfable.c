#include "global.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_effect_helpers.h"
#include "field_player_avatar.h"
#include "field_weather.h"
#include "main.h"
#include "party_menu.h"
#include "sprite.h"
#include "surfable.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/field_effects.h"
#include "constants/moves.h"
#include "constants/species.h"

extern const struct OamData gObjectEventBaseOam_32x32;
extern const struct OamData gObjectEventBaseOam_64x64;
extern const struct SpriteTemplate *const gFieldEffectObjectTemplatePointers[];

extern void SynchroniseSurfAnim(struct ObjectEvent *playerObj, struct Sprite *sprite);
extern void SynchroniseSurfPosition(struct ObjectEvent *playerObj, struct Sprite *sprite);
void HnsFollowerReleaseForSurf(void); /* HNS_FOLLOWER_ROOM_PATCH_V2: event_object_movement.c */

static void CreateOverlaySprite(bool8 isShiny);
static void UpdateSurfMonOverlay(struct Sprite *sprite);

struct RideablePokemon
{
    u16 species;
    u8 trainerPose;
};

#include "data/object_events/surfable/surfable_pokemon.h"
#include "data/object_events/surfable/surfable_pokemon_graphics.h"
#include "data/object_events/surfable/surfable_pokemon_pic_tables.h"
#include "data/object_events/surfable/surfable_pokemon_templates.h"

/* SWIM_STAGE81: two independent ocean visual pairs; Stage80 species lookup. */
static bool8 SwimV3AcquirePalette(u16 speciesIndex, u8 variant, u8 *outSlot, u16 *outTag);
void SwimV3ReleasePalette(u16 tag);

#define SWIM_STAGE79_ENABLE 1
#if SWIM_STAGE79_ENABLE
#include "wild_encounter_ow.h"
#include "constants/metatile_behaviors.h"
#define SWIM81_SLOTS 2

struct Swim81Visual
{
    u8 main, overlay, parent;
    u8 localId, mapNum, mapGroup;
    u16 graphicsId, paletteTag;
    u8 lastDirection;
    bool8 active;
};
static EWRAM_DATA struct Swim81Visual sSwim81[SWIM81_SLOTS];
static void Swim81SpriteCB(struct Sprite *sprite);

static bool8 Swim81Valid(u8 n)
{
    struct Swim81Visual *v = &sSwim81[n];
    u32 i;
    if (!v->active || v->parent >= MAX_SPRITES ||
        !gSprites[v->parent].inUse)
        return FALSE;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *obj = &gObjectEvents[i];
        if (obj->active && obj->spriteId == v->parent &&
            obj->localId == v->localId && obj->mapNum == v->mapNum &&
            obj->mapGroup == v->mapGroup && obj->graphicsId == v->graphicsId &&
            obj->currentMetatileBehavior == MB_OCEAN_WATER &&
            IsOverworldWildEncounter(obj, OWE_GENERATED))
            return TRUE;
    }
    return FALSE;
}

static void Swim81Cleanup(u8 n)
{
    struct Swim81Visual *v = &sSwim81[n];
    u8 main = v->main, overlay = v->overlay;
    u16 tag = v->paletteTag;
    v->active = FALSE;
    v->main = MAX_SPRITES;
    v->overlay = MAX_SPRITES;
    v->parent = MAX_SPRITES;
    v->paletteTag = 0;
    if (main < MAX_SPRITES && gSprites[main].inUse &&
        gSprites[main].callback == Swim81SpriteCB &&
        gSprites[main].data[0] == n)
        DestroySprite(&gSprites[main]);
    if (overlay < MAX_SPRITES && gSprites[overlay].inUse &&
        gSprites[overlay].callback == Swim81SpriteCB &&
        gSprites[overlay].data[0] == n)
        DestroySprite(&gSprites[overlay]);
    if (tag != 0)
        SwimV3ReleasePalette(tag);
}

static void Swim81SpriteCB(struct Sprite *sprite)
{
    u8 n = sprite->data[0];
    struct Swim81Visual *v;
    struct ObjectEvent *obj = NULL;
    struct Sprite *parent;
    u8 anim = 0;
    u32 i;
    if (n >= SWIM81_SLOTS)
        return;
    v = &sSwim81[n];
    if (!Swim81Valid(n) || v->main >= MAX_SPRITES ||
        v->overlay >= MAX_SPRITES ||
        !gSprites[v->main].inUse || !gSprites[v->overlay].inUse ||
        gSprites[v->main].callback != Swim81SpriteCB ||
        gSprites[v->overlay].callback != Swim81SpriteCB ||
        gSprites[v->main].data[0] != n ||
        gSprites[v->overlay].data[0] != n)
    {
        Swim81Cleanup(n);
        return;
    }
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *candidate = &gObjectEvents[i];
        if (candidate->active && candidate->spriteId == v->parent &&
            candidate->localId == v->localId &&
            candidate->mapNum == v->mapNum && candidate->mapGroup == v->mapGroup &&
            candidate->graphicsId == v->graphicsId)
        {
            obj = candidate;
            break;
        }
    }
    if (obj == NULL) { Swim81Cleanup(n); return; }
    parent = &gSprites[v->parent];
    sprite->x = parent->x;
    sprite->y = parent->y;
    sprite->x2 = parent->x2;
    sprite->y2 = parent->y2;
    sprite->oam.priority = parent->oam.priority;
    sprite->subpriority = parent->subpriority;
    sprite->invisible = obj->invisible || obj->offScreen;
    switch (obj->facingDirection)
    {
    case DIR_NORTH: anim = 1; break;
    case DIR_WEST: anim = 2; break;
    case DIR_EAST: anim = 3; break;
    default: anim = 0; break;
    }
    if (v->lastDirection != obj->facingDirection)
    {
        StartSpriteAnim(&gSprites[v->main], anim);
        StartSpriteAnim(&gSprites[v->overlay], anim);
        v->lastDirection = obj->facingDirection;
    }
}

void Swim79UpdateObject(struct ObjectEvent *obj)
{
    u32 i, n;
    u8 palSlot, main, overlay;
    u16 tag;
    const struct SpriteTemplate *mainTemplate, *overlayTemplate;
    bool8 shiny;
    struct Swim81Visual *v;
    if (obj == NULL || obj->spriteId >= MAX_SPRITES)
        return;
    /* Sweep both slots, including stale visuals from a prior map. */
    for (n = 0; n < SWIM81_SLOTS; n++)
    {
        v = &sSwim81[n];
        if (v->active && !Swim81Valid(n))
            Swim81Cleanup(n);
        if (v->active && v->parent == obj->spriteId &&
            v->localId == obj->localId && v->mapNum == obj->mapNum &&
            v->mapGroup == obj->mapGroup && v->graphicsId == obj->graphicsId)
        {
            gSprites[obj->spriteId].invisible = TRUE;
            return;
        }
    }
    if (!IsOverworldWildEncounter(obj, OWE_GENERATED) ||
        obj->currentMetatileBehavior != MB_OCEAN_WATER ||
        !(obj->graphicsId & OBJ_EVENT_MON) ||
        !gSprites[obj->spriteId].inUse)
        return;
    for (n = 0; n < SWIM81_SLOTS; n++)
        if (!sSwim81[n].active) break;
    if (n == SWIM81_SLOTS)
        return;
    for (i = 0; i < ARRAY_COUNT(gSurfablePokemon); i++)
        if (gSurfablePokemon[i].species ==
            (obj->graphicsId & OBJ_EVENT_MON_SPECIES_MASK)) break;
    if (i == ARRAY_COUNT(gSurfablePokemon)) return;
    shiny = (obj->graphicsId & OBJ_EVENT_MON_SHINY) != 0;
    mainTemplate = shiny ? &gSurfablePokemonOverworldShinySprites[i] : &gSurfablePokemonOverworldSprites[i];
    overlayTemplate = shiny ? &gSurfablePokemonOverlayShinySprites[i] : &gSurfablePokemonOverlaySprites[i];
    if (mainTemplate->tileTag != TAG_NONE || overlayTemplate->tileTag != TAG_NONE ||
        !mainTemplate->images || !overlayTemplate->images) return;
    if (!SwimV3AcquirePalette(i, shiny ? 1 : 0, &palSlot, &tag)) return;
    main = CreateSpriteAtEnd(mainTemplate, 0, 0, 0);
    if (main == MAX_SPRITES) { SwimV3ReleasePalette(tag); return; }
    overlay = CreateSpriteAtEnd(overlayTemplate, 0, 0, 0);
    if (overlay == MAX_SPRITES)
    {
        DestroySprite(&gSprites[main]);
        SwimV3ReleasePalette(tag);
        return;
    }
    v = &sSwim81[n];
    v->main = main; v->overlay = overlay; v->parent = obj->spriteId;
    v->localId = obj->localId; v->mapNum = obj->mapNum;
    v->mapGroup = obj->mapGroup; v->graphicsId = obj->graphicsId;
    v->paletteTag = tag; v->lastDirection = 0xFF;
    gSprites[main].callback = Swim81SpriteCB;
    gSprites[overlay].callback = Swim81SpriteCB;
    gSprites[main].data[0] = n;
    gSprites[overlay].data[0] = n;
    gSprites[main].coordOffsetEnabled = TRUE;
    gSprites[overlay].coordOffsetEnabled = TRUE;
    gSprites[main].oam.paletteNum = palSlot;
    gSprites[overlay].oam.paletteNum = palSlot;
    v->active = TRUE;
    Swim81SpriteCB(&gSprites[main]);
    Swim81SpriteCB(&gSprites[overlay]);
    if (v->active) gSprites[obj->spriteId].invisible = TRUE;
}
#else
void Swim79UpdateObject(struct ObjectEvent *obj) { (void)obj; }
#endif
/* END SWIM_STAGE79 */

/* HNS_FOLLOWER_ROOM_PATCH_V2: does this species get a swim sprite?  (used by the encounter spawner) */
bool8 Swim81SpeciesHasVisual(u16 species)
{
    u32 i;

    for (i = 0; i < ARRAY_COUNT(gSurfablePokemon); i++)
    {
        if (gSurfablePokemon[i].species == species)
            return TRUE;
    }
    return FALSE;
}


static EWRAM_DATA u16 sCurrentSurfMon = {0};

/* SWIM_V3_COMBINED: palette leases inactive until all teardown hooks exist. */
/* Swimming V3 palette lease prototype. NOT WIRED INTO GAMEPLAY.
 * Requires surfable.c palette tables and sprite.h functions.
 * This module must be integrated only after sprite destruction and reset
 * are wired to SwimV3ReleasePalette and SwimV3ResetPaletteLeases.
 */
#define SWIM_V3_PALETTE_LEASES 2
#define SWIM_V3_PALETTE_TAG_BASE 0x7700
struct SwimV3PaletteLease
{
    u16 speciesIndex;
    u16 tag;
    u8 variant;
    u8 slot;
    u8 refs;
    bool8 active;
};
static struct SwimV3PaletteLease sSwimV3PaletteLeases[SWIM_V3_PALETTE_LEASES];

/* No lease may be acquired unless all teardown paths have been integrated. */
static bool8 SwimV3AcquirePalette(u16 speciesIndex, u8 variant, u8 *outSlot, u16 *outTag)
{
    u32 i;
    u32 slot;
    u16 tag;
    const struct SpritePalette *pal;
    if (outSlot == NULL || outTag == NULL ||
        speciesIndex >= ARRAY_COUNT(sSurfablePokemonPalettes) || variant > 2)
        return FALSE;
    for (i = 0; i < SWIM_V3_PALETTE_LEASES; i++)
    {
        struct SwimV3PaletteLease *lease = &sSwimV3PaletteLeases[i];
        if (lease->active && lease->speciesIndex == speciesIndex && lease->variant == variant)
        {
            if (lease->refs == 255 || IndexOfSpritePaletteTag(lease->tag) != lease->slot)
                return FALSE;
            lease->refs++;
            *outSlot = lease->slot;
            *outTag = lease->tag;
            return TRUE;
        }
    }
    for (i = 0; i < SWIM_V3_PALETTE_LEASES; i++)
        if (!sSwimV3PaletteLeases[i].active)
            break;
    if (i == SWIM_V3_PALETTE_LEASES)
        return FALSE;
    tag = SWIM_V3_PALETTE_TAG_BASE + i;
    /* Never adopt another system's palette, even if the tag matches. */
    if (IndexOfSpritePaletteTag(tag) != 0xFF)
        return FALSE;
    pal = variant == 2 ? &sSurfablePokemonShinyModernPalettes[speciesIndex]
        : variant == 1 ? &sSurfablePokemonShinyPalettes[speciesIndex]
        : &sSurfablePokemonPalettes[speciesIndex];
    if (pal->data == NULL)
        return FALSE;
    slot = LoadSpritePaletteWithTag(pal->data, tag);
    if (slot >= 16)
    {
        /* Failed acquisition: release only a tag we just registered. */
        if (IndexOfSpritePaletteTag(tag) != 0xFF)
            FreeSpritePaletteByTag(tag);
        return FALSE;
    }
    /* Stage33: do not publish a lease if the allocator returned a
     * slot that is not owned by this tag, or one already leased. */
    if (IndexOfSpritePaletteTag(tag) != slot)
        return FALSE;
    {
        u32 j;
        for (j = 0; j < SWIM_V3_PALETTE_LEASES; j++)
        {
            if (sSwimV3PaletteLeases[j].active &&
                sSwimV3PaletteLeases[j].slot == slot)
            {
                /* Release only our own freshly loaded tag. */
                if (IndexOfSpritePaletteTag(tag) == slot)
                    FreeSpritePaletteByTag(tag);
                return FALSE;
            }
        }
    }
    /* Apply weather correction before publishing the lease. */
    UpdateSpritePaletteWithWeather(slot, TRUE);
    sSwimV3PaletteLeases[i].speciesIndex = speciesIndex;
    sSwimV3PaletteLeases[i].variant = variant;
    sSwimV3PaletteLeases[i].tag = tag;
    sSwimV3PaletteLeases[i].slot = slot;
    sSwimV3PaletteLeases[i].refs = 1;
    sSwimV3PaletteLeases[i].active = TRUE;
    *outSlot = slot;
    *outTag = tag;
    return TRUE;
}

/* SWIM_V3_STAGE29: require a live lease before sprite commit. */
bool8 SwimV3HasPaletteLease(u16 tag, u8 slot)
{
    u32 i;
    if (slot >= 16)
        return FALSE;
    for (i = 0; i < SWIM_V3_PALETTE_LEASES; i++)
    {
        const struct SwimV3PaletteLease *lease = &sSwimV3PaletteLeases[i];
        if (lease->active && lease->refs != 0 &&
            lease->tag == tag && lease->slot == slot &&
            IndexOfSpritePaletteTag(tag) == slot)
            return TRUE;
    }
    return FALSE;
}

void SwimV3ReleasePalette(u16 tag)
{
    u32 i;
    for (i = 0; i < SWIM_V3_PALETTE_LEASES; i++)
    {
        struct SwimV3PaletteLease *lease = &sSwimV3PaletteLeases[i];
        if (!lease->active || lease->tag != tag)
            continue;
        if (lease->refs == 0)
            return;
        if (--lease->refs == 0)
        {
            /* Never free a palette whose slot was reassigned externally. */
            if (IndexOfSpritePaletteTag(tag) == lease->slot)
                FreeSpritePaletteByTag(tag);
            lease->active = FALSE;
        }
        return;
    }
}

/* Called after sprite-side leases are released during ResetSpriteData. */
void SwimV3ResetPaletteLeases(void)
{
    u32 i;
    for (i = 0; i < SWIM_V3_PALETTE_LEASES; i++)
        sSwimV3PaletteLeases[i].active = FALSE;
}


/* SWIM_V3_STAGE52: sheet-backed compressed OW conversion. */
extern bool8 SwimV3SwapSheetGraphics(struct Sprite *sprite,
                                     const struct SpriteTemplate *visual,
                                     u8 paletteNum, u8 facingAnim, u16 paletteTag);

/* SWIM_V3_STAGE15: Tentacool-only, no-overlay graphics conversion entry point.
 * Deliberately NOT called from gameplay until an ARM build and lifecycle test.
 * One object sprite is reused; no extra sprite allocation.
 */
bool8 SwimV3TryConvertTentacool(struct Sprite *sprite, bool8 isShiny, u8 facingAnim)
{
    u32 i;
    u32 frameIndex;
    u8 slot;
    u16 tag;
    const struct SpriteTemplate *visual;
    if (sprite == NULL || !sprite->inUse || facingAnim >= 4)
        return FALSE;
    for (i = 0; i < ARRAY_COUNT(gSurfablePokemon); i++)
        if (gSurfablePokemon[i].species == SPECIES_TENTACOOL)
            break;
    if (i == ARRAY_COUNT(gSurfablePokemon) ||
        i >= ARRAY_COUNT(gSurfablePokemonOverworldSprites) ||
        i >= ARRAY_COUNT(gSurfablePokemonOverworldShinySprites) ||
        i >= ARRAY_COUNT(gSurfablePokemonOverlaySprites) ||
        i >= ARRAY_COUNT(gSurfablePokemonOverlayShinySprites))
        return FALSE;
    /* Stage 29: deliberately use only the six base frames.
     * Never create or attach the player Surf overlay to a wild object. */
    visual = isShiny ? &gSurfablePokemonOverworldShinySprites[i]
                     : &gSurfablePokemonOverworldSprites[i];
    /* SWIM_V3_STAGE29: all six Tentacool base frames must fit allocated tiles.
     * The Tentacool Surf base tables contain exactly six entries (0..5).
     * Validate BEFORE obtaining a palette lease or allocating sprite tiles.
     */
    if (visual->images == NULL || visual->images[0].size == 0)
        return FALSE;
    for (frameIndex = 0; frameIndex < 6; frameIndex++)
    {
        if (visual->images[frameIndex].data == NULL ||
            visual->images[frameIndex].size == 0 ||
            visual->images[frameIndex].size > visual->images[0].size)
            return FALSE;
    }
    /* SWIM_V3_STAGE33: fail before acquiring a palette for ineligible sprites. */
    if (sprite->images == NULL || sprite->animPaused ||
        sprite->oam.affineMode != 0 || visual->oam == NULL ||
        visual->oam->affineMode != 0 ||
        sprite->oam.bpp != visual->oam->bpp)
        return FALSE;
    if (!SwimV3AcquirePalette(i, isShiny ? 1 : 0, &slot, &tag))
        return FALSE;
    /* Ownership transfers to the sprite only on successful commit. */
    if (!(sprite->usingSheet
          ? SwimV3SwapSheetGraphics(sprite, visual, slot, facingAnim, tag)
          : SwimV3SwapDynamicGraphics(sprite, visual, slot, facingAnim, tag)))
    {
        SwimV3ReleasePalette(tag);
        return FALSE;
    }
    return TRUE;
}

static u16 GetSurfMonSpecies(void)
{
    u8 i;
	u16 species;
	
	i = VarGet(VAR_SURF_MON_SLOT);
	species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
    return species;
}

static u16 GetSurfablePokemonSprite(void)
{
    u8 i;
    u16 mon = GetSurfMonSpecies();

    for (i = 0; i < ARRAY_COUNT(gSurfablePokemon); i++)
    {
        if (mon == gSurfablePokemon[i].species)
            return i;
    }
    return 0xFFFF;
}

static void LoadSurfOverworldPalette(bool8 isShiny)
{
    u8 palette;

    if (isShiny == TRUE
        && gSaveBlock3Ptr != NULL
        && gSaveBlock3Ptr->challengeSettings.tx_Features_ShinyColors)
        palette = LoadSpritePalette(&sSurfablePokemonShinyModernPalettes[sCurrentSurfMon]);
    else if (isShiny == TRUE)
        palette = LoadSpritePalette(&sSurfablePokemonShinyPalettes[sCurrentSurfMon]);
    else
        palette = LoadSpritePalette(&sSurfablePokemonPalettes[sCurrentSurfMon]);

    UpdateSpritePaletteWithWeather(palette, TRUE);
}

u32 CreateSurfablePokemonSprite(void)
{
    u8 spriteId;
    bool8 isShiny;
    struct Sprite *sprite;

    /* HNS_FOLLOWER_ROOM_PATCH_V2: a hidden big follower must not hold ~448 tiles while the
     * Surf Pokemon's sprites are allocated. */
    HnsFollowerReleaseForSurf();

    isShiny = IsMonShiny(&gPlayerParty[VarGet(VAR_SURF_MON_SLOT)]);

    SetSpritePosToOffsetMapCoords((s16 *)&gFieldEffectArguments[0], (s16 *)&gFieldEffectArguments[1], 8, 8);

    sCurrentSurfMon = GetSurfablePokemonSprite();
    if (sCurrentSurfMon != 0xFFFF)
    {
        LoadSurfOverworldPalette(isShiny);

        if (isShiny == TRUE)
            spriteId = CreateSpriteAtEnd(&gSurfablePokemonOverworldShinySprites[sCurrentSurfMon], gFieldEffectArguments[0], gFieldEffectArguments[1], 0x96);
        else
            spriteId = CreateSpriteAtEnd(&gSurfablePokemonOverworldSprites[sCurrentSurfMon], gFieldEffectArguments[0], gFieldEffectArguments[1], 0x96);

        if (gSurfablePokemonOverlaySprites[sCurrentSurfMon].tileTag == 0xFFFF)
        {
            CreateOverlaySprite(isShiny);
        }
    }
    else
    { // Create surf blob
        spriteId = CreateSpriteAtEnd(gFieldEffectObjectTemplatePointers[FLDEFFOBJ_SURF_BLOB], gFieldEffectArguments[0], gFieldEffectArguments[1], 0x96);
    }

    if (spriteId != MAX_SPRITES)
    {
        sprite = &gSprites[spriteId];
        sprite->coordOffsetEnabled = TRUE;
        if (sCurrentSurfMon == 0xFFFF)
            sprite->oam.paletteNum = LoadPlayerObjectEventPalette(gSaveBlock2Ptr->playerGender);
        sprite->data[2] = gFieldEffectArguments[2];
        sprite->data[3] = -1;
        sprite->data[6] = -1;
        sprite->data[7] = -1;
    }
    FieldEffectActiveListRemove(FLDEFF_SURF_BLOB);
    return spriteId;
}

static void CreateOverlaySprite(bool8 isShiny)
{
    u8 overlaySprite;
    u8 subpriority;
    struct Sprite *sprite;

    subpriority = gSprites[gPlayerAvatar.spriteId].subpriority - 1;
    if (isShiny == TRUE)
        overlaySprite = CreateSpriteAtEnd(&gSurfablePokemonOverlayShinySprites[sCurrentSurfMon], gFieldEffectArguments[0], gFieldEffectArguments[1], subpriority);
    else
        overlaySprite = CreateSpriteAtEnd(&gSurfablePokemonOverlaySprites[sCurrentSurfMon], gFieldEffectArguments[0], gFieldEffectArguments[1], subpriority);

    if (overlaySprite != MAX_SPRITES)
    {
        sprite = &gSprites[overlaySprite];
        sprite->coordOffsetEnabled = TRUE;
        sprite->data[2] = gFieldEffectArguments[2];
        sprite->data[3] = -1;
        sprite->data[6] = -1;
        sprite->data[7] = -1;
        sprite->oam.priority = 2;
    }
    SetSurfBlob_BobState(overlaySprite, BOB_PLAYER_AND_MON);
}

static void UpdateSurfMonOverlay(struct Sprite *sprite)
{
    struct ObjectEvent *playerObj;
    struct Sprite *linkedSprite;
    u8 subpriority;
	
    playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
    linkedSprite = &gSprites[playerObj->spriteId];

    SynchroniseSurfAnim(playerObj, sprite);
    SynchroniseSurfPosition(playerObj, sprite);

    // Ensure Fishing whilst surfing has overlay sprite "bob" up and down appropriately
    if (VarGet(VAR_FREEZE_SURF_BLOB) == 0)
	{
        UpdateBobbingEffect(playerObj, linkedSprite, sprite);
    }

    // Reset the subpriority for the overlay sprite so it shows on top of the player
    // We need this here so the subprio is correct after a screen transition (e.g. after exiting a battle)
    subpriority = gSprites[gPlayerAvatar.spriteId].subpriority - 1;
    sprite->subpriority = subpriority;

    if (linkedSprite->animNum < MOVEMENT_ACTION_DELAY_16)
    {
        sprite->x = linkedSprite->x;
        sprite->y = linkedSprite->y + 8;
        sprite->y2 = linkedSprite->y2;
    }
    if (!(gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_SURFING))
        DestroySprite(sprite);
}

