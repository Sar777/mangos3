/*
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "DBCStores.h"
#include "Unit.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Creature.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellMgr.h"

/*#######################################
########                         ########
########   PLAYERS STAT SYSTEM   ########
########                         ########
#######################################*/

bool Player::UpdateStats(Stats stat)
{
    if(stat > STAT_SPIRIT)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);

    SetStat(stat, int32(value));

    if (IsInWorld())
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_STAT, stat, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);

    switch(stat)
    {
        case STAT_STRENGTH:
            break;
        case STAT_AGILITY:
            UpdateArmor();
            UpdateAllCritPercentages();
            UpdateDodgePercentage();
            break;
        case STAT_STAMINA:   UpdateMaxHealth(); break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);
            UpdateAllSpellCritChances();
            UpdateArmor();                                  //SPELL_AURA_MOD_RESISTANCE_OF_INTELLECT_PERCENT, only armor currently
            break;

        case STAT_SPIRIT:
            break;

        default:
            break;
    }
    // Need update (exist AP from stat auras)
    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);

    UpdateSpellDamageAndHealingBonus();
    UpdateManaRegen();

    // Update ratings in exist SPELL_AURA_MOD_RATING_FROM_STAT and only depends from stat
    uint32 mask = 0;
    AuraList const& modRatingFromStat = GetAurasByType(SPELL_AURA_MOD_RATING_FROM_STAT);
    for(AuraList::const_iterator i = modRatingFromStat.begin();i != modRatingFromStat.end(); ++i)
        if (Stats((*i)->GetMiscBValue()) == stat)
            mask |= (*i)->GetMiscValue();
    if (mask)
    {
        for (uint32 rating = 0; rating < MAX_COMBAT_RATING; ++rating)
            if (mask & (1 << rating))
                ApplyRatingMod(CombatRating(rating), 0, true);
    }
    return true;
}

void Player::ApplySpellPowerBonus(int32 amount, bool apply)
{
    m_baseSpellPower+=apply?amount:-amount;

    // For speed just update for client
    ApplyModUInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, amount, apply);
    for(int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        ApplyModUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS+i, amount, apply);;

    if (IsInWorld())
    {
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_ATTACKPOWER, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_SPELLDAMAGE, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
    }
}

void Player::UpdateSpellDamageAndHealingBonus()
{
    // Magic damage modifiers implemented in Unit::SpellDamageBonusDone
    // This information for client side use only
    // Get healing bonus for all schools
    SetStatInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL));
    // Get damage bonus for all schools
    // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG field handled in Aura::HandleModDamageDone
    for(int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        SetStatInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS+i, SpellBaseDamageBonusDone(SpellSchoolMask(1 << i)) - GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+i));

    CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_ATTACKPOWER, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
    CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_SPELLDAMAGE, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
}

bool Player::UpdateAllStats()
{
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        float value = GetTotalStatValue(Stats(i));
        SetStat(Stats(i), (int32)value);
    }

    UpdateArmor();
    // calls UpdateAttackPowerAndDamage() in UpdateArmor for SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR
    UpdateAttackPowerAndDamage(true);
    UpdateMaxHealth();

    for (uint32 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllRatings();
    UpdateAllCritPercentages();
    UpdateAllSpellCritChances();
    UpdateBlockPercentage();
    UpdateParryPercentage();
    UpdateShieldBlockDamageValue();
    UpdateDodgePercentage();
    UpdateArmorPenetration();
    UpdateSpellDamageAndHealingBonus();
    UpdateManaRegen();
    UpdateExpertise(BASE_ATTACK);
    UpdateExpertise(OFF_ATTACK);
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    return true;
}

void Player::UpdateResistances(uint32 school)
{
    if(school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();

    CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_RESISTANCE, school, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
}

void Player::UpdateArmor()
{
    float value = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    value  = GetModifierValue(unitMod, BASE_VALUE);         // base armor (from items)
    value *= GetModifierValue(unitMod, BASE_PCT);           // armor percent from items
    value += GetModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    AuraList const& mResbyIntellect = GetAurasByType(SPELL_AURA_MOD_RESISTANCE_OF_STAT_PERCENT);
    for(AuraList::const_iterator i = mResbyIntellect.begin();i != mResbyIntellect.end(); ++i)
    {
        Modifier const* mod = (*i)->GetModifier();
        if(mod->m_miscvalue & SPELL_SCHOOL_MASK_NORMAL)
            value += int32(GetStat(Stats((*i)->GetMiscBValue())) * mod->m_amount / 100.0f);
    }

    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));

    UpdateAttackPowerAndDamage();                           // armor dependent auras update for SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR
}

float Player::GetHealthBonusFromStamina()
{
    GtOCTHpPerStaminaEntry const* hpBase = sGtOCTHpPerStaminaStore.LookupEntry((getClass() - 1) * GT_MAX_LEVEL + getLevel() - 1);

    float stamina = GetStat(STAT_STAMINA);

    float baseStam = stamina < 20 ? stamina : 20;
    float moreStam = stamina - baseStam;
    if (moreStam < 0.0f)
        moreStam = 0.0f;

    return baseStam + moreStam * hpBase->ratio;
}

float Player::GetManaBonusFromIntellect()
{
    float intellect = GetStat(STAT_INTELLECT);

    float baseInt = intellect < 20 ? intellect : 20;
    float moreInt = intellect - baseInt;

    return baseInt + (moreInt*15.0f);
}

void Player::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;

    float value = GetModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) + GetHealthBonusFromStamina();
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Player::UpdateMaxPower(Powers power)
{
    MANGOS_ASSERT(power < MAX_POWERS);

    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    uint32 create_power = GetCreateMaxPowers(power);

    // ignore classes without mana
    float bonusPower = (power == POWER_MANA && create_power > 0) ? GetManaBonusFromIntellect() : 0;

    float value = GetModifierValue(unitMod, BASE_VALUE) + create_power;
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) +  bonusPower;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

void Player::UpdateAttackPowerAndDamage(bool ranged )
{
    ChrClassesEntry const * chrEntry = sChrClassesStore.LookupEntry(getClass());
    MANGOS_ASSERT(chrEntry);

    float val2 = 0.0f;
    float level = float(getLevel());

    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 index_mod = UNIT_FIELD_ATTACK_POWER_MOD_POS;
    uint16 index_mult = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if(ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        index_mod = UNIT_FIELD_RANGED_ATTACK_POWER_MOD_POS;
        index_mult = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;

        float rapPerAgi = std::max(GetStat(STAT_AGILITY) - 10.0f, 0.0f) * chrEntry->rapPerAgi;

        switch(getClass())
        {
            case CLASS_HUNTER: val2 =  level * 2.0f + rapPerAgi;    break;
            case CLASS_ROGUE:  val2 =  level        + rapPerAgi;    break;
            case CLASS_WARRIOR: val2 = level        + rapPerAgi;    break;
            default: break;
        }
    }
    else
    {
        float apPerAgi = std::max(GetStat(STAT_AGILITY) - 10.0f, 0.0f) * chrEntry->apPerAgi;
        float apPerStr = std::max(GetStat(STAT_STRENGTH) - 10.0f, 0.0f) * chrEntry->apPerStr;
        float levelmod;
        switch(getClass())
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_DEATH_KNIGHT:
            case CLASS_DRUID:
                levelmod = 3.0f;
                break;
            default:
                levelmod = 2.0f;
                break;
        }

        val2 = level * levelmod + apPerAgi + apPerStr;

        // extracted from client
        if (getClass() == CLASS_DRUID && GetShapeshiftForm())
                {
            if (SpellShapeshiftFormEntry const * entry = sSpellShapeshiftFormStore.LookupEntry(uint32(GetShapeshiftForm())))
                if (entry->flags1 & 0x20)
                    val2 += std::max(GetStat(STAT_AGILITY) - 10.0f, 0.0f) * chrEntry->apPerStr;
        }
    }

    SetModifierValue(unitMod, BASE_VALUE, val2);

    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    if( ranged )
    {
        if ((getClassMask() & CLASSMASK_WAND_USERS)==0)
        {
            AuraList const& mRAPbyStat = GetAurasByType(SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT);
            for(AuraList::const_iterator i = mRAPbyStat.begin();i != mRAPbyStat.end(); ++i)
                attPowerMod += int32(GetStat(Stats((*i)->GetModifier()->m_miscvalue)) * (*i)->GetModifier()->m_amount / 100.0f);
        }
    }
    else
    {
        AuraList const& mAPbyStat = GetAurasByType(SPELL_AURA_MOD_ATTACK_POWER_OF_STAT_PERCENT);
        for(AuraList::const_iterator i = mAPbyStat.begin();i != mAPbyStat.end(); ++i)
            attPowerMod += int32(GetStat(Stats((*i)->GetModifier()->m_miscvalue)) * (*i)->GetModifier()->m_amount / 100.0f);

        AuraList const& mAPbyArmor = GetAurasByType(SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR);
        for(AuraList::const_iterator iter = mAPbyArmor.begin(); iter != mAPbyArmor.end(); ++iter)
            // always: ((*i)->GetModifier()->m_miscvalue == 1 == SPELL_SCHOOL_MASK_NORMAL)
            attPowerMod += int32(GetArmor() / (*iter)->GetModifier()->m_amount);
    }

    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, (uint32)base_attPower);            //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(index_mod, (uint32)attPowerMod);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetFloatValue(index_mult, attPowerMultiplier);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field

    //automatically update weapon damage after attack power modification
    if(ranged)
    {
        UpdateDamagePhysical(RANGED_ATTACK);
    }
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        if(CanDualWield() && haveOffhandWeapon())           //allow update offhand damage only if player knows DualWield Spec and has equipped offhand weapon
            UpdateDamagePhysical(OFF_ATTACK);
    }

    UpdateSpellDamageAndHealingBonus();

    if (IsInWorld())
    {
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_ATTACKPOWER, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_SPELLDAMAGE, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_DAMAGE, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
    }
}

void Player::UpdateShieldBlockDamageValue()
{
    SetUInt32Value(PLAYER_SHIELD_BLOCK, GetShieldBlockDamageValue());
}

void Player::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, float& min_damage, float& max_damage)
{
    UnitMods unitMod;
    UnitMods attPower;

    switch(attType)
    {
        case BASE_ATTACK:
        default:
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            attPower = UNIT_MOD_ATTACK_POWER;
            break;
        case OFF_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            attPower = UNIT_MOD_ATTACK_POWER;
            break;
        case RANGED_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            attPower = UNIT_MOD_ATTACK_POWER_RANGED;
            break;
    }

    float att_speed = GetAPMultiplier(attType,normalized);

    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType)/ 14.0f * att_speed;
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    if (IsInFeralForm())                                    // check if player is druid and in cat or bear forms, non main hand attacks not allowed for this mode so not check attack type
    {
        float weaponSpeed = GetAttackTime(attType) / 1000.0f;

        switch (GetShapeshiftForm())
        {
            case FORM_CAT:
                weapon_mindamage = weapon_mindamage / weaponSpeed;
                weapon_maxdamage = weapon_maxdamage / weaponSpeed;
                break;
            case FORM_BEAR:
                weapon_mindamage = weapon_mindamage / weaponSpeed + weapon_mindamage / 2.5f;
                weapon_maxdamage = weapon_maxdamage / weaponSpeed + weapon_maxdamage / 2.5f;
                break;
        }
    }
    else if (!CanUseEquippedWeapon(attType))                // check if player not in form but still can't use weapon (broken/etc)
    {
        weapon_mindamage = BASE_MINDAMAGE;
        weapon_maxdamage = BASE_MAXDAMAGE;
    }
    else if (attType == RANGED_ATTACK)                      // add ammo DPS to ranged damage
    {
        weapon_mindamage += GetAmmoDPS() * att_speed;
        weapon_maxdamage += GetAmmoDPS() * att_speed;
    }

    min_damage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    max_damage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;
}

void Player::UpdateDamagePhysical(WeaponAttackType attType)
{
    float mindamage;
    float maxdamage;

    CalculateMinMaxDamage(attType,false,mindamage,maxdamage);

    switch(attType)
    {
        case BASE_ATTACK:
        default:
            SetStatFloatValue(UNIT_FIELD_MINDAMAGE,mindamage);
            SetStatFloatValue(UNIT_FIELD_MAXDAMAGE,maxdamage);
            break;
        case OFF_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE,mindamage);
            SetStatFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE,maxdamage);
            break;
        case RANGED_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINRANGEDDAMAGE,mindamage);
            SetStatFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE,maxdamage);
            break;
    }
}

void Player::UpdateBlockPercentage()
{
    // No block
    float value = 0.0f;
    if(CanBlock())
    {
        // Base value
        value = 5.0f;
        // Increase from SPELL_AURA_MOD_BLOCK_CHANCE_PERCENT aura
        value += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_CHANCE_PERCENT);
        // Increase from rating (exists only on auras)
        value += GetRatingBonusValue(CR_BLOCK);
        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_BLOCK_PERCENTAGE, value);
}

void Player::UpdateCritPercentage(WeaponAttackType attType)
{
    BaseModGroup modGroup;
    uint16 index;
    CombatRating cr;

    switch(attType)
    {
        case OFF_ATTACK:
            modGroup = OFFHAND_CRIT_PERCENTAGE;
            index = PLAYER_OFFHAND_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
        case RANGED_ATTACK:
            modGroup = RANGED_CRIT_PERCENTAGE;
            index = PLAYER_RANGED_CRIT_PERCENTAGE;
            cr = CR_CRIT_RANGED;
            break;
        case BASE_ATTACK:
        default:
            modGroup = CRIT_PERCENTAGE;
            index = PLAYER_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
    }

    float value = GetTotalPercentageModValue(modGroup) + GetRatingBonusValue(cr);
    // Modify crit from weapon skill and maximized defense skill of same level victim difference
    value += (int32(GetMaxSkillValueForLevel()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(index, value);
}

void Player::UpdateAllCritPercentages()
{
    float value = GetMeleeCritFromAgility();

    SetBaseModValue(CRIT_PERCENTAGE, PCT_MOD, value);
    SetBaseModValue(OFFHAND_CRIT_PERCENTAGE, PCT_MOD, value);
    SetBaseModValue(RANGED_CRIT_PERCENTAGE, PCT_MOD, value);

    UpdateCritPercentage(BASE_ATTACK);
    UpdateCritPercentage(OFF_ATTACK);
    UpdateCritPercentage(RANGED_ATTACK);
}

const float Player::m_diminishing_k[MAX_CLASSES] =
{
    0.9560f,  // Warrior
    0.9560f,  // Paladin
    0.9880f,  // Hunter
    0.9880f,  // Rogue
    0.9830f,  // Priest
    0.9560f,  // DK
    0.9880f,  // Shaman
    0.9830f,  // Mage
    0.9830f,  // Warlock
    0.0f,     // ??
    0.9720f   // Druid
};

void Player::UpdateParryPercentage()
{
    const float parry_cap[MAX_CLASSES] =
    {
        65.631440f,     // Warrior
        65.631440f,     // Paladin
        145.560408f,  // Hunter
        145.560408f,  // Rogue
          0.0f,       // Priest
        65.631440f,     // DK
        145.560408f,  // Shaman
          0.0f,       // Mage
          0.0f,       // Warlock
          0.0f,       // ??
          0.0f        // Druid
    };

    // No parry
    float value = 0.0f;
    uint32 pclass = getClass()-1;
    if (CanParry() && parry_cap[pclass] > 0.0f)
    {
        // Base parry
        float nondiminishing  = 5.0f;
        float diminishing = 0.0f;
        GetParryFromStrength(diminishing, nondiminishing);
        // Parry from rating
        diminishing += GetRatingBonusValue(CR_PARRY);
        // Parry from SPELL_AURA_MOD_PARRY_PERCENT aura
        nondiminishing += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        // apply diminishing formula to diminishing parry chance
        value = nondiminishing + diminishing * parry_cap[pclass] /
                                 (diminishing + parry_cap[pclass] * m_diminishing_k[pclass]);
        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_PARRY_PERCENTAGE, value);
}

void Player::UpdateDodgePercentage()
{
    const float dodge_cap[MAX_CLASSES] =
    {
        65.631440f,     // Warrior
        65.631440f,     // Paladin
        145.560408f,  // Hunter
        145.560408f,  // Rogue
        150.375940f,  // Priest
        65.631440f,     // DK
        145.560408f,  // Shaman
        150.375940f,  // Mage
        150.375940f,  // Warlock
          0.0f,       // ??
        116.890707f   // Druid
    };

    float diminishing = 0.0f;
    float nondiminishing = 0.0f;
    // Dodge from agility
    GetDodgeFromAgility(diminishing, nondiminishing);
    // Dodge from SPELL_AURA_MOD_DODGE_PERCENT aura
    nondiminishing += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
    // Dodge from rating
    diminishing += GetRatingBonusValue(CR_DODGE);
    // apply diminishing formula to diminishing dodge chance
    uint32 pclass = getClass()-1;
    float value = nondiminishing + (diminishing * dodge_cap[pclass] /
                                    (diminishing + dodge_cap[pclass] * m_diminishing_k[pclass]));
    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(PLAYER_DODGE_PERCENTAGE, value);
}

void Player::UpdateSpellCritChance(uint32 school)
{
    // For normal school set zero crit chance
    if(school == SPELL_SCHOOL_NORMAL)
    {
        SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1, 0.0f);
        return;
    }
    // For others recalculate it from:
    float crit = 0.0f;
    // Crit from Intellect
    crit += GetSpellCritFromIntellect();
    // Increase crit from SPELL_AURA_MOD_SPELL_CRIT_CHANCE
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_CRIT_CHANCE);
    // Increase crit from SPELL_AURA_MOD_ALL_CRIT_CHANCE
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_ALL_CRIT_CHANCE);
    // Increase crit by school from SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL
    crit += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, 1<<school);
    // Increase crit from spell crit ratings
    crit += GetRatingBonusValue(CR_CRIT_SPELL);

    // Store crit value
    SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + school, crit);
}

void Player::UpdateMeleeHitChances()
{
    m_modMeleeHitChance = GetTotalAuraModifier(SPELL_AURA_MOD_HIT_CHANCE);
    m_modMeleeHitChance+=  GetRatingBonusValue(CR_HIT_MELEE);

    CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_HIT, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
}

void Player::UpdateRangedHitChances()
{
    m_modRangedHitChance = GetTotalAuraModifier(SPELL_AURA_MOD_HIT_CHANCE);
    m_modRangedHitChance+= GetRatingBonusValue(CR_HIT_RANGED);
}

void Player::UpdateSpellHitChances()
{
    m_modSpellHitChance = GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_HIT_CHANCE);
    m_modSpellHitChance+= GetRatingBonusValue(CR_HIT_SPELL);

    if (IsInWorld())
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_SPELLHIT, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
}

void Player::UpdateAllSpellCritChances()
{
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateSpellCritChance(i);
}

void Player::UpdateExpertise(WeaponAttackType attack)
{
    if(attack==RANGED_ATTACK)
        return;

    int32 expertise = int32(GetRatingBonusValue(CR_EXPERTISE));

    Item *weapon = GetWeaponForAttack(attack);

    AuraList const& expAuras = GetAurasByType(SPELL_AURA_MOD_EXPERTISE);
    for(AuraList::const_iterator itr = expAuras.begin(); itr != expAuras.end(); ++itr)
    {
        // item neutral spell
        if ((*itr)->GetSpellProto()->GetEquippedItemClass() == -1)
            expertise += (*itr)->GetModifier()->m_amount;
        // item dependent spell
        else if(weapon && weapon->IsFitToSpellRequirements((*itr)->GetSpellProto()))
            expertise += (*itr)->GetModifier()->m_amount;
    }

    if(expertise < 0)
        expertise = 0;

    switch(attack)
    {
        case BASE_ATTACK: SetUInt32Value(PLAYER_EXPERTISE, expertise);         break;
        case OFF_ATTACK:  SetUInt32Value(PLAYER_OFFHAND_EXPERTISE, expertise); break;
        default: break;
    }

    if (IsInWorld())
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_EXPERTIZE, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
}

void Player::UpdateArmorPenetration()
{
    m_armorPenetrationPct = GetRatingBonusValue(CR_ARMOR_PENETRATION);

    AuraList const& armorAuras = GetAurasByType(SPELL_AURA_MOD_TARGET_ARMOR_PCT);
    for(AuraList::const_iterator itr = armorAuras.begin(); itr != armorAuras.end(); ++itr)
    {
        // affects all weapons
        if ((*itr)->GetSpellProto()->GetEquippedItemClass() == -1)
        {
            m_armorPenetrationPct += (*itr)->GetModifier()->m_amount;
            continue;
        }

        // dependent on weapon class
        for(uint8 i = 0; i < MAX_ATTACK; ++i)
        {
            Item *weapon = GetWeaponForAttack(WeaponAttackType(i));
            if(weapon && weapon->IsFitToSpellRequirements((*itr)->GetSpellProto()))
            {
                m_armorPenetrationPct += (*itr)->GetModifier()->m_amount;
                break;
            }
        }
    }
}

void Player::ApplyHealthRegenBonus(int32 amount, bool apply)
{
    m_baseHealthRegen+= apply ? amount : -amount;
}

void Player::ApplyManaRegenBonus(int32 amount, bool apply)
{
    m_baseManaRegen+= apply ? amount : -amount;
    UpdateManaRegen();
}

void Player::UpdateManaRegen()
{
    float base_regen = GetCreateMana() * 0.01f;

    // Mana regen from spirit and intellect
    float spirit_regen = sqrt(GetStat(STAT_INTELLECT)) * OCTRegenMPPerSpirit();
    // Apply PCT bonus from SPELL_AURA_MOD_POWER_REGEN_PERCENT aura on spirit base regen
    spirit_regen *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, POWER_MANA);

    // Mana regen from SPELL_AURA_MOD_POWER_REGEN aura
    float power_regen_mp5 = GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA) / 5.0f;

    // Get bonus from SPELL_AURA_MOD_MANA_REGEN_FROM_STAT aura
    AuraList const& regenAura = GetAurasByType(SPELL_AURA_MOD_MANA_REGEN_FROM_STAT);
    for(AuraList::const_iterator i = regenAura.begin();i != regenAura.end(); ++i)
    {
        Modifier const* mod = (*i)->GetModifier();
        power_regen_mp5 += GetStat(Stats(mod->m_miscvalue)) * mod->m_amount / 500.0f;
    }

    // Set regen rate in cast state apply only on spirit based regen
    int32 modManaRegenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
    if (modManaRegenInterrupt > 100)
        modManaRegenInterrupt = 100;

    SetStatFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER, base_regen + power_regen_mp5 + spirit_regen * modManaRegenInterrupt / 100.0f);
    SetStatFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER, base_regen + 0.001f + power_regen_mp5 + spirit_regen);

    if (IsInWorld())
        CallForAllControlledUnits(ApplyScalingBonusWithHelper(SCALING_TARGET_POWERREGEN, 0, false),CONTROLLED_PET|CONTROLLED_GUARDIANS);
}

void Player::UpdateMasteryAuras()
{
    if (!HasAuraType(SPELL_AURA_MASTERY))
    {
        SetFloatValue(PLAYER_MASTERY, 0.0f);
        return;
    }

    float masteryValue = GetTotalAuraModifier(SPELL_AURA_MASTERY) + GetRatingBonusValue(CR_MASTERY);
    SetFloatValue(PLAYER_MASTERY, masteryValue);

    std::vector<uint32> const* masterySpells = GetTalentTreeMasterySpells(m_talentsPrimaryTree[m_activeSpec]);
    if (!masterySpells)
        return;

    for (uint32 i = 0; i < masterySpells->size(); ++i)
    {
        SpellAuraHolderPtr holder = GetSpellAuraHolder(masterySpells->at(i));
        if (!holder)
            continue;

        SpellEntry const* spellEntry = holder->GetSpellProto();

        // calculate mastery scaling coef
        int32 masteryCoef = GetMasteryCoefficient(spellEntry);
        if (!masteryCoef)
            continue;

        // update aura modifiers
        for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            Aura* aura = holder->GetAuraByEffectIndex(SpellEffectIndex(j));
            if (!aura)
                continue;

            if (spellEntry->CalculateSimpleValue(SpellEffectIndex(j)))
                continue;

            aura->ApplyModifier(false, false);
            aura->GetModifier()->m_amount = int32(masteryValue * masteryCoef / 100.0f);
            aura->ApplyModifier(true, false);
        }
    }
}

void Player::_ApplyAllStatBonuses()
{
    SetCanModifyStats(false);

    _ApplyAllAuraMods();
    _ApplyAllItemMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

void Player::_RemoveAllStatBonuses()
{
    SetCanModifyStats(false);

    _RemoveAllItemMods();
    _RemoveAllAuraMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

/*#######################################
########                         ########
########    MOBS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Creature::UpdateStats(Stats /*stat*/)
{
    return true;
}

bool Creature::UpdateAllStats()
{
    UpdateMaxHealth();
    UpdateAttackPowerAndDamage();

    for (uint32 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    for(int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    return true;
}

void Creature::UpdateResistances(uint32 school)
{
    if(school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Creature::UpdateArmor()
{
    float value = GetTotalAuraModValue(UNIT_MOD_ARMOR);
    SetArmor(int32(value));
}

void Creature::UpdateMaxHealth()
{
    float value = GetTotalAuraModValue(UNIT_MOD_HEALTH);
    SetMaxHealth((uint32)value);
}

void Creature::UpdateMaxPower(Powers power)
{
    MANGOS_ASSERT(power < MAX_POWERS);

    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float value  = GetTotalAuraModValue(unitMod);
    SetMaxPower(power, uint32(value));
}

void Creature::UpdateAttackPowerAndDamage(bool ranged)
{
    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 index_mod = UNIT_FIELD_ATTACK_POWER_MOD_POS;
    uint16 index_mult = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if (ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        index_mod = UNIT_FIELD_RANGED_ATTACK_POWER_MOD_POS;
        index_mult = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;
    }

    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, (uint32)base_attPower);            //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(index_mod, (uint32)attPowerMod);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetFloatValue(index_mult, attPowerMultiplier);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field

    if (ranged)
        return;

    //automatically update weapon damage after attack power modification
    UpdateDamagePhysical(BASE_ATTACK);
    UpdateDamagePhysical(OFF_ATTACK);
}

void Creature::UpdateDamagePhysical(WeaponAttackType attType)
{
    if (attType > OFF_ATTACK)
        return;

    UnitMods unitMod = (attType == BASE_ATTACK ? UNIT_MOD_DAMAGE_MAINHAND : UNIT_MOD_DAMAGE_OFFHAND);

    /* difference in AP between current attack power and base value from DB */
    float att_pwr_change = GetTotalAttackPowerValue(attType) - GetCreatureInfo()->attackpower;
    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + (att_pwr_change * GetAPMultiplier(attType, false) / 14.0f);
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);
    float dmg_multiplier = GetCreatureInfo()->dmg_multiplier;

    float weapon_mindamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    float mindamage = ((base_value + weapon_mindamage) * dmg_multiplier * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * dmg_multiplier * base_pct + total_value) * total_pct;

    SetStatFloatValue(attType == BASE_ATTACK ? UNIT_FIELD_MINDAMAGE : UNIT_FIELD_MINOFFHANDDAMAGE, mindamage);
    SetStatFloatValue(attType == BASE_ATTACK ? UNIT_FIELD_MAXDAMAGE : UNIT_FIELD_MAXOFFHANDDAMAGE, maxdamage);
}

/*#######################################
########                         ########
########    PETS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Pet::UpdateStats(Stats stat)
{
    if(stat > STAT_SPIRIT || stat < STAT_STRENGTH )
        return false;

    // value = ((create_value + base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);
    SetStat(stat, int32(value));

    switch(stat)
    {
        case STAT_STRENGTH:
            UpdateAttackPowerAndDamage();
            break;
        case STAT_AGILITY:
            UpdateAttackPowerAndDamage(true);
            UpdateArmor();
            break;
        case STAT_STAMINA:
            UpdateMaxHealth();
            break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);
            break;
        case STAT_SPIRIT:
        default:
            break;
    }
    return true;
}

bool Pet::UpdateAllStats()
{
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        UpdateStats(Stats(i));

    for (uint32 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);
    UpdateManaRegen();
    UpdateSpellPower();

    return true;
}

void Pet::UpdateResistances(uint32 school)
{
    if(school > SPELL_SCHOOL_NORMAL)
    {
        SetResistance(SpellSchools(school),
                      int32(GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school))));
    }
    else
        UpdateArmor();
}

void Pet::UpdateArmor()
{
    float value = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    value  = GetModifierValue(unitMod, BASE_VALUE) + GetStat(STAT_AGILITY) * 2.0f;
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE);
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));
}

void Pet::UpdateMaxHealth()
{
    if (!CanModifyStats())
        return;

    UnitMods unitMod = UNIT_MOD_HEALTH;
    float staminaBonus = (GetStat(STAT_STAMINA) - GetCreateStat(STAT_STAMINA)) * (CalculateScalingData()->healthScale / 100.0f);

    float value   = GetModifierValue(unitMod, BASE_VALUE) + GetCreateHealth() + staminaBonus;
    value  *= GetModifierValue(unitMod, BASE_PCT);
    value  += GetModifierValue(unitMod, TOTAL_VALUE);
    value  *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Pet::UpdateMaxPower(Powers power)
{
    if (!CanModifyStats())
        return;

    MANGOS_ASSERT(power < MAX_POWERS);

    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float intellectBonus = (power == POWER_MANA) ? (GetStat(STAT_INTELLECT) - GetCreateStat(STAT_INTELLECT))*(CalculateScalingData()->powerScale / 100.0f) : 0.0f;

    float value  = GetModifierValue(unitMod, BASE_VALUE) + GetCreatePowers(power) /*+ intellectBonus*/;
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE);
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

void Pet::UpdateAttackPowerAndDamage(bool ranged)
{
    UnitMods unitMod;
    float baseAP       = 0.0f;

    if (!ranged)
    {
        unitMod  = UNIT_MOD_ATTACK_POWER;
        baseAP = (CalculateScalingData()->APBaseScale / 100.0f) * (GetStat(STAT_STRENGTH) - CalculateScalingData()->APBasepoint);
    }
    else
    {
        unitMod  = UNIT_MOD_ATTACK_POWER_RANGED;
        baseAP = (CalculateScalingData()->APBaseScale / 100.0f) * (GetStat(STAT_AGILITY) - CalculateScalingData()->APBasepoint);
    }

    if ((baseAP) < 0.0f)
        baseAP = 0.0f;

    SetModifierValue(unitMod, BASE_VALUE, baseAP);

    //in BASE_VALUE of UNIT_MOD_ATTACK_POWER for creatures we store data of meleeattackpower field in DB
    float base_attPower      = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod        = GetModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    // UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER, (int32)base_attPower);
    //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER_MOD_POS, (int32)attPowerMod);
    //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field
    SetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER, attPowerMultiplier);

    // automatically update weapon damage after attack power modification
    UpdateDamagePhysical(BASE_ATTACK);
}

void Pet::UpdateDamagePhysical(WeaponAttackType attType)
{
    UnitMods unitMod;

    if (attType == BASE_ATTACK)
        unitMod  = UNIT_MOD_DAMAGE_MAINHAND;
    else if (attType == RANGED_ATTACK)
       unitMod  = UNIT_MOD_DAMAGE_RANGED;
    else
        return;

    float att_speed = float(GetAttackTime(attType))/1000.0f;

    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType)/ 14.0f * att_speed;
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    float mindamage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;

    if (attType == BASE_ATTACK)
    {
        SetStatFloatValue(UNIT_FIELD_MINDAMAGE, mindamage);
        SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxdamage);
    }
    else if (attType == RANGED_ATTACK)
    {
        SetStatFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, mindamage);
        SetStatFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, maxdamage);
    }
}

void Pet::UpdateSpellPower()
{
    Unit* owner = GetOwner();

    if(!owner ||owner->GetTypeId()!=TYPEID_PLAYER || !owner->IsInWorld())
        return;

    MAPLOCK_READ(owner,MAP_LOCK_TYPE_AURAS);
                                                  // Only for displaying in client!
    owner->SetUInt32Value(PLAYER_PET_SPELL_POWER, SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_SPELL));
}

void Pet::UpdateManaRegen()
{
    float power_regen = sqrt(GetStat(STAT_INTELLECT)) * OCTRegenMPPerSpirit();

    power_regen *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, POWER_MANA);

    // Mana regen from SPELL_AURA_MOD_POWER_REGEN aura
    float power_regen_mp5 = GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA) / 5.0f;

    // Set regen rate in cast state apply only on spirit based regen
    int32 modManaRegenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);

    SetStatFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER, power_regen_mp5 + power_regen * modManaRegenInterrupt / 100.0f);

    SetStatFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER, power_regen_mp5 + power_regen);
}
