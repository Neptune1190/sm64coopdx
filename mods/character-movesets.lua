
gStateExtras = {}
for i=0,(MAX_PLAYERS-1) do
    gStateExtras[i] = {}
    local m = gMarioStates[i]
    local e = gStateExtras[i]
    e.lastAction = m.action
    e.animFrame = 0
    e.scuttle = 0
    e.averageForwardVel = 0
    e.boostTimer = 0
    e.rotAngle = 0
    e.lastHurtCounter = 0
end

gEventTable = {}

-----------
-- luigi --
-----------

function luigi_before_phys_step(m)
    local e = gStateExtras[m.playerIndex]

    local hScale = 1.0
    local vScale = 1.0

    -- faster swimming
    if (m.action & ACT_FLAG_SWIMMING) ~= 0 then
        hScale = hScale * 1.25
    end

    -- slower holding item
    if m.heldObj ~= nil then
        m.vel.y = m.vel.y - 2.0
        hScale = hScale * 0.9
        if (m.action & ACT_FLAG_AIR) ~= 0 then
            hScale = hScale * 0.9
        end
    end
    m.forwardVel = m.forwardVel * 0.99

    m.vel.x = m.vel.x * hScale
    m.vel.y = m.vel.y * vScale
    m.vel.z = m.vel.z * hScale
end

function luigi_on_set_action(m)
    local e = gStateExtras[m.playerIndex]

    -- extra height to the backflip
    if m.action == ACT_BACKFLIP then
        m.vel.y = m.vel.y + 18

    -- nerf wall kicks
    elseif m.action == ACT_WALL_KICK_AIR and m.prevAction ~= ACT_HOLDING_POLE and m.prevAction ~= ACT_CLIMBING_POLE then
        if m.vel.y > 30 then m.vel.y = 30 end
        return

    -- turn dive into kick when holding jump
    elseif m.action == ACT_DIVE and (m.controller.buttonDown & A_BUTTON) ~= 0 and e.scuttle > 0 then
        return set_mario_action(m, ACT_JUMP_KICK, 0)

    -- extra height on jumps
    elseif m.action == ACT_JUMP or m.action == ACT_DOUBLE_JUMP or m.action == ACT_TRIPLE_JUMP or m.action == ACT_SPECIAL_TRIPLE_JUMP or m.action == ACT_STEEP_JUMP or m.action == ACT_SIDE_FLIP or m.action == ACT_RIDING_SHELL_JUMP then
        m.vel.y = m.vel.y + 6

    end

    e.lastAction = action
end

function luigi_update(m)
    local e = gStateExtras[m.playerIndex]

    -- air scuttle
    e.scuttle = 0
    if m.action == ACT_JUMP or m.action == ACT_DOUBLE_JUMP then
        if (m.controller.buttonDown & A_BUTTON) ~= 0 and m.vel.y < -5 then
            m.vel.y = m.vel.y + 3
            set_mario_animation(m, MARIO_ANIM_RUNNING_UNUSED)
            set_anim_to_frame(m, e.animFrame)
            e.animFrame = e.animFrame + 13
            if e.animFrame >= m.marioObj.header.gfx.animInfo.curAnim.loopEnd then
                e.animFrame = e.animFrame - m.marioObj.header.gfx.animInfo.curAnim.loopEnd
            end
            e.scuttle = 1
        end
    end

    -- backflip turns into twirl
    if m.action == ACT_BACKFLIP and m.marioObj.header.gfx.animInfo.animFrame > 18 then
        m.angleVel.y = 0x1800
        set_mario_action(m, ACT_TWIRLING, 1)
    end
end

gEventTable[CT_LUIGI] = {
    before_phys_step = luigi_before_phys_step,
    on_set_action    = luigi_on_set_action,
    update           = luigi_update,
}

-----------
-- toad --
-----------

function toad_before_phys_step(m)
    local e = gStateExtras[m.playerIndex]

    local hScale = 1.0
    local vScale = 1.0

    -- faster ground movement
    if (m.action & ACT_FLAG_MOVING) ~= 0 then
        hScale = hScale * 1.19
    end

    -- slower holding item
    if m.heldObj ~= nil then
        m.vel.y = m.vel.y - 2.0
        hScale = hScale * 0.9
        if (m.action & ACT_FLAG_AIR) ~= 0 then
            hScale = hScale * 0.9
        end
    end

    m.vel.x = m.vel.x * hScale
    m.vel.y = m.vel.y * vScale
    m.vel.z = m.vel.z * hScale
end

function toad_on_set_action(m)
    local e = gStateExtras[m.playerIndex]

    -- wall kick height based on how fast toad is going
    if m.action == ACT_WALL_KICK_AIR and m.prevAction ~= ACT_HOLDING_POLE and m.prevAction ~= ACT_CLIMBING_POLE then
        m.vel.y = m.vel.y * 0.5
        m.vel.y = m.vel.y + e.averageForwardVel * 0.7
        return
    end

    -- more distance on dive and long jump
    if m.action == ACT_DIVE or m.action == ACT_LONG_JUMP then
        m.forwardVel = m.forwardVel * 1.35
    end

    -- less height on jumps
    if m.action == ACT_JUMP or m.action == ACT_DOUBLE_JUMP or m.action == ACT_TRIPLE_JUMP or m.action == ACT_SPECIAL_TRIPLE_JUMP or m.action == ACT_STEEP_JUMP or m.action == ACT_SIDE_FLIP or m.action == ACT_RIDING_SHELL_JUMP or m.action == ACT_BACKFLIP or m.action == ACT_WALL_KICK_AIR  or m.action == ACT_LONG_JUMP then
        m.vel.y = m.vel.y * 0.8
    end

    e.lastAction = action
end

function toad_update(m)
    local e = gStateExtras[m.playerIndex]
    if e.averageForwardVel > m.forwardVel then
        e.averageForwardVel = e.averageForwardVel * 0.93 + m.forwardVel * 0.07
    else
        e.averageForwardVel = m.forwardVel
    end

    -- faster flip during ground pound
    if m.action == ACT_GROUND_POUND then
        if m.actionTimer < 10 then
            m.actionTimer = m.actionTimer + 1
        end
    end

    -- ground pound jump
    if m.action == ACT_GROUND_POUND_LAND and (m.input & INPUT_A_PRESSED) ~= 0 then
        set_mario_action(m, ACT_TRIPLE_JUMP, 0)
        m.vel.y = m.vel.y + 18
        m.forwardVel = m.forwardVel + 10
    end

end

gEventTable[CT_TOAD] = {
    before_phys_step = toad_before_phys_step,
    on_set_action    = toad_on_set_action,
    update           = toad_update,
}

-----------
-- waluigi --
-----------

ACT_WALL_SLIDE = (0x0BF | ACT_FLAG_AIR | ACT_FLAG_MOVING | ACT_FLAG_ALLOW_VERTICAL_WIND_ACTION)

function act_wall_slide(m)
    if (m.input & INPUT_A_PRESSED) ~= 0 then
        local rc = set_mario_action(m, ACT_TRIPLE_JUMP, 0)
        m.vel.y = 72.0

        if m.forwardVel < 20.0 then
            m.forwardVel = 20.0
        end
        m.wallKickTimer = 0
        return rc
    end

    -- attempt to stick to the wall a bit. if it's 0, sometimes you'll get kicked off of slightly sloped walls
    mario_set_forward_vel(m, -1.0)

    m.particleFlags = m.particleFlags | PARTICLE_DUST

    play_sound(SOUND_MOVING_TERRAIN_SLIDE + m.terrainSoundAddend, m.marioObj.header.gfx.cameraToObject)
    set_mario_animation(m, MARIO_ANIM_START_WALLKICK)

    if perform_air_step(m, 0) == AIR_STEP_LANDED then
        mario_set_forward_vel(m, 0.0)
        if check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB) == 0 then
            return set_mario_action(m, ACT_FREEFALL_LAND, 0)
        end
    end

    m.actionTimer = m.actionTimer + 1
    if m.wall == nil and m.actionTimer > 2 then
        mario_set_forward_vel(m, 0.0)
        return set_mario_action(m, ACT_FREEFALL, 0)
    end

    -- gravity
    m.vel.y = m.vel.y + 2

    return 0
end

function waluigi_before_phys_step(m)
    local e = gStateExtras[m.playerIndex]

    local hScale = 1.0
    local vScale = 1.0

    -- faster ground movement
    if (m.action & ACT_FLAG_MOVING) ~= 0 then
        hScale = hScale * 1.085
    end

    m.vel.x = m.vel.x * hScale
    m.vel.y = m.vel.y * vScale
    m.vel.z = m.vel.z * hScale

    if m.action == ACT_TRIPLE_JUMP and m.prevAction == ACT_DOUBLE_JUMP and m.actionTimer < 6 then
        m.particleFlags = m.particleFlags | PARTICLE_DUST
        m.actionTimer = m.actionTimer + 1
    end
end

function waluigi_on_set_action(m)
    local e = gStateExtras[m.playerIndex]

    -- wall slide
    if m.action == ACT_SOFT_BONK then
        m.faceAngle.y = m.faceAngle.y + 0x8000
        set_mario_action(m, ACT_WALL_SLIDE, 0)
        m.vel.x = 0
        m.vel.y = 0
        m.vel.z = 0

    -- turn wall kick into flip
    elseif m.action == ACT_WALL_KICK_AIR and m.prevAction ~= ACT_HOLDING_POLE and m.prevAction ~= ACT_CLIMBING_POLE then
        local rc = set_mario_action(m, ACT_TRIPLE_JUMP, 0)
        m.vel.y = 72.0

        if m.forwardVel < 20.0 then
            m.forwardVel = 20.0
        end
        m.wallKickTimer = 0
        return rc

    -- less height on jumps
    elseif m.action == ACT_JUMP or m.action == ACT_DOUBLE_JUMP or m.action == ACT_TRIPLE_JUMP or m.action == ACT_SPECIAL_TRIPLE_JUMP or m.action == ACT_STEEP_JUMP or m.action == ACT_SIDE_FLIP or m.action == ACT_RIDING_SHELL_JUMP or m.action == ACT_BACKFLIP or m.action == ACT_WALL_KICK_AIR  or m.action == ACT_LONG_JUMP then
        m.vel.y = m.vel.y * 0.93
    end

    e.lastAction = action
end

function waluigi_update(m)
    local e = gStateExtras[m.playerIndex]

    -- increase player damage
    if m.hurtCounter > e.lastHurtCounter then
        m.hurtCounter = m.hurtCounter * 2
    end
    e.lastHurtCounter = m.hurtCounter

    -- double jump
    if m.action == ACT_DOUBLE_JUMP and m.actionTimer > 0 and (m.controller.buttonPressed & A_BUTTON) ~= 0 then
        set_mario_action(m, ACT_TRIPLE_JUMP, 0)
        m.vel.y = m.vel.y * 0.94
    end
    if m.action == ACT_DOUBLE_JUMP then
        m.actionTimer = m.actionTimer + 1
    end

end

gEventTable[CT_WALUIGI] = {
    before_phys_step = waluigi_before_phys_step,
    on_set_action    = waluigi_on_set_action,
    update           = waluigi_update,
}

----------
-- main --
----------

function mario_before_phys_step(m)
    if gEventTable[m.character.type] == nil then
        return
    end
    gEventTable[m.character.type].before_phys_step(m)
end

function mario_on_set_action(m)
    if gEventTable[m.character.type] == nil then
        return
    end
    gEventTable[m.character.type].on_set_action(m)
end

function mario_update(m)
    if gEventTable[m.character.type] == nil then
        return
    end
    gEventTable[m.character.type].update(m)
end

-----------
-- hooks --
-----------

hook_event(HOOK_MARIO_UPDATE, mario_update)
hook_event(HOOK_ON_SET_MARIO_ACTION, mario_on_set_action)
hook_event(HOOK_BEFORE_PHYS_STEP, mario_before_phys_step)

hook_mario_action(ACT_WALL_SLIDE, act_wall_slide)
