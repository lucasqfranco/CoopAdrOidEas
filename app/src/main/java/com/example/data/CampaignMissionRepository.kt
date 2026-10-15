package com.example.data

import com.example.model.CampaignAct
import com.example.model.Mission
import com.example.model.MissionCheckpoint
import com.example.model.MissionDifficulty

object CampaignMissionRepository {

    val allMissions: List<Mission> = listOf(
        // ACT 1: LOS SANTOS
        Mission(
            id = "ls_01",
            title = "Big Smoke & Ryder",
            bossOrGiver = "Sean 'Sweet' Johnson",
            act = CampaignAct.ACT_1,
            synopsis = "Carl returns to Grove Street for his mother's funeral. Reconnect with Sweet, Smoke, and Ryder, and escape Ballas drive-by on bicycles.",
            coOpRole1 = "Lead Cyclist (Navigates alleys & bunny-hops roadblocks)",
            coOpRole2 = "Wingman (Blocks Ballas Voodoo car & covers rear)",
            difficulty = MissionDifficulty.EASY,
            cashReward = 200,
            respectReward = 15,
            unlockedVehiclesOrGifts = "BMX Bikes unlocked at Ganton",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Depart Beverly Johnson's House", "Mount BMX and follow Sweet", "Provide rear lookout for purple Ballas cars"),
                MissionCheckpoint("cp2", "Evade Ballas Drive-By Ambush", "Lead sprint through the storm drain aqueduct", "Screen teammate from pursuers"),
                MissionCheckpoint("cp3", "Return safely to Grove Street Cul-de-Sac", "Park bike in Johnson driveway", "Regroup with Ryder and Smoke")
            ),
            isCompleted = true,
            isAvailable = true
        ),
        Mission(
            id = "ls_02",
            title = "Drive-Thru",
            bossOrGiver = "Melvin 'Big Smoke'",
            act = CampaignAct.ACT_1,
            synopsis = "Pick up food at Cluckin' Bell. Smoke orders two number 9s. Ballas drive-by car is spotted heading for Grove Street — intercept and eliminate them before they reach the hood.",
            coOpRole1 = "Wheelman (Drives Greenwood, performs pit maneuvers)",
            coOpRole2 = "Turret Gunner (Rolls down window, targets tires and engine block)",
            difficulty = MissionDifficulty.MEDIUM,
            cashReward = 500,
            respectReward = 25,
            unlockedVehiclesOrGifts = "Grove Street Greenwood Tuning unlocked",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Order at Cluckin' Bell Drive-Thru", "Pull into order speaker", "Watch rear traffic for purple Glendale"),
                MissionCheckpoint("cp2", "Pursue fleeing Ballas Glendale", "Ram and pin vehicle against highway divider", "Focus fire on driver through windshield"),
                MissionCheckpoint("cp3", "Deliver crew back to Ganton unharmed", "Speed back to Smoke's house", "Keep vehicle health above 40%")
            ),
            isCompleted = true,
            isAvailable = true
        ),
        Mission(
            id = "ls_03",
            title = "Drive-By",
            bossOrGiver = "Sean 'Sweet' Johnson",
            act = CampaignAct.ACT_1,
            synopsis = "Take the fight to the enemy. Hit four Ballas turf clusters across Idlewood and Glen Park from the vehicle, then shake off a 2-star police response.",
            coOpRole1 = "Assault Driver (Positions vehicle for optimal passenger firing angles)",
            coOpRole2 = "SMG Gunner (Eliminates purple gang clusters on sidewalk)",
            difficulty = MissionDifficulty.MEDIUM,
            cashReward = 1200,
            respectReward = 40,
            unlockedVehiclesOrGifts = "Submachine guns available in Ammu-Nation",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Assault Ballas corner in Idlewood", "Slow down to 20 mph for gunner stability", "Eliminate 4 Ballas sentries"),
                MissionCheckpoint("cp2", "Sweep Glen Park territory", "Circle the park perimeter", "Target Ballas reinforcements on park benches"),
                MissionCheckpoint("cp3", "Spray down vehicle at Pay 'n' Spray", "Navigate into Idlewood Pay 'n' Spray shop", "Hold fire to avoid resetting police search")
            ),
            isCompleted = false,
            isAvailable = true
        ),
        Mission(
            id = "ls_04",
            title = "Just Business",
            bossOrGiver = "Melvin 'Big Smoke'",
            act = CampaignAct.ACT_1,
            synopsis = "Escort Big Smoke to an Atrium deal that goes sour with the Russian Mafia. Clear the multi-level atrium, then survive a high-octane motorbike chase through the drainage canals.",
            coOpRole1 = "Motorcycle Pilot (Drives PCJ-600, avoids roadblocks & big rigs)",
            coOpRole2 = "Rear Gunner (Unlimited ammo TEC-9, destroys chasing sedans & bikes)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 2500,
            respectReward = 60,
            unlockedVehiclesOrGifts = "Heavy weapons cache in Commerce",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Clear Russian Mafia in Commerce Atrium", "Sweep ground floor behind marble pillars", "Snipe upper balcony riflemen"),
                MissionCheckpoint("cp2", "Board PCJ-600 in alleyway", "Navigate sewer jump ramps", "Shoot out tires of pursuing Packers"),
                MissionCheckpoint("cp3", "The Flood Control Channel Showdown", "Jump the bridge barricade ramp", "Blow up oil tankers blocking canal escape")
            ),
            isCompleted = false,
            isAvailable = true
        ),
        Mission(
            id = "ls_05",
            title = "Reuniting the Families",
            bossOrGiver = "Sean 'Sweet' Johnson",
            act = CampaignAct.ACT_1,
            synopsis = "Sweet convenes a peace conference at Jefferson Motel. LSPD SWAT raids the motel with helicopters. Carl breaches the hotel to rescue Sweet, followed by a legendary highway shootout.",
            coOpRole1 = "Breach & Rescue Specialist (Combat Shotgun room-clearing)",
            coOpRole2 = "Rooftop Sniper & Heavy Support (Downs SWAT helicopters)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 3000,
            respectReward = 80,
            unlockedVehiclesOrGifts = "Body Armor pickups at Grove Cul-de-Sac",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Breach Jefferson Motel lobby", "Sweep ground floor hallways", "Take out SWAT rappelling through skylights"),
                MissionCheckpoint("cp2", "Rescue Sweet on 2nd floor", "Breach Sweet's motel suite door", "Cover Sweet during resuscitation"),
                MissionCheckpoint("cp3", "Highway Shootout on Greenwood", "Dodge police cruisers on Mulholland Intersection", "Shoot SWAT climbing onto hood and trunk")
            ),
            isCompleted = false,
            isAvailable = true
        ),
        Mission(
            id = "ls_06",
            title = "The Green Sabre",
            bossOrGiver = "Cesar Vialpando",
            act = CampaignAct.ACT_1,
            synopsis = "Cesar reveals Smoke and Ryder's betrayal behind the Green Sabre. Sweet is trapped in a massive Ballas ambush under the freeway intersection. Rush to save him against overwhelming odds.",
            coOpRole1 = "Tactical Driver (Speed rush through LS traffic)",
            coOpRole2 = "Heavy Gunner (M4 / AK-47 suppression against Ballas swarm)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 0,
            respectReward = 100,
            unlockedVehiclesOrGifts = "Act I Climax: Unlocks Act II Badlands",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Meet Cesar under Verdant Bluffs freeway", "Inspect Green Sabre garage meeting", "Provide perimeter watch"),
                MissionCheckpoint("cp2", "Race to Mulholland Intersection in 3 mins", "Speed through East Los Santos streets", "Arm assault rifles"),
                MissionCheckpoint("cp3", "Defend wounded Sweet against 4 Ballas waves", "Set defensive perimeter at highway embankment", "Eliminate waves of purple Greenwood cars")
            ),
            isCompleted = false,
            isAvailable = false
        ),

        // ACT 2: THE BADLANDS
        Mission(
            id = "bl_01",
            title = "Badlands",
            bossOrGiver = "Officer Frank Tenpenny",
            act = CampaignAct.ACT_2,
            synopsis = "Dropped off in Whetstone by C.R.A.S.H. Scale Mount Chiliad to eliminate an FBI-protected witness and photograph his corpse before federal agents evacuate him.",
            coOpRole1 = "Mountain Off-Roader (Navigates Sanchez up Chiliad trails)",
            coOpRole2 = "Stealth Recon (Suppressed pistol & Camera recon)",
            difficulty = MissionDifficulty.MEDIUM,
            cashReward = 1500,
            respectReward = 20,
            unlockedVehiclesOrGifts = "Mountain Bike & Sanchez on Mt. Chiliad summit",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Ascend Mount Chiliad on dirt bikes", "Lead off-road ascent without tumbling down ravine", "Cover team from federal patrols"),
                MissionCheckpoint("cp2", "Infiltrate cabin on mountain summit", "Flank federal agents behind wood cabins", "Eliminate witness before car escape"),
                MissionCheckpoint("cp3", "Photograph body & deliver film", "Snap clear photograph of target", "Escape downhill to Angel Pine trailer park")
            ),
            isCompleted = false,
            isAvailable = false
        ),
        Mission(
            id = "bl_02",
            title = "Body Harvest",
            bossOrGiver = "The Truth",
            act = CampaignAct.ACT_2,
            synopsis = "Steal a high-powered Combine Harvester from armed survivalist cultists at an isolated Red County farm to pay off The Truth's debts.",
            coOpRole1 = "Harvester Hijacker (Board and drive the heavy machine)",
            coOpRole2 = "Combat Escort (Eliminate hillbilly survivalists on tractors)",
            difficulty = MissionDifficulty.MEDIUM,
            cashReward = 2000,
            respectReward = 30,
            unlockedVehiclesOrGifts = "The Truth's 'Mothership' Camper unlocked",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Infiltrate cultist farm in Flint County", "Breach main wheat fields", "Neutralize riflemen in guard towers"),
                MissionCheckpoint("cp2", "Steal the Combine Harvester", "Drive harvester through fields", "Protect harvester blades from rifle fire"),
                MissionCheckpoint("cp3", "Drive slow harvester to Truth's barn", "Plow through Walton pickup trucks", "Park safely in Truth's greenhouse shed")
            ),
            isCompleted = false,
            isAvailable = false
        ),
        Mission(
            id = "bl_03",
            title = "Are You Going to San Fierro?",
            bossOrGiver = "The Truth",
            act = CampaignAct.ACT_2,
            synopsis = "DEA choppers raid The Truth's weed farm. Use flamethrowers to torch all marijuana fields before police arrive, then shoot down the police helicopter with an RPG.",
            coOpRole1 = "Pyro Operator (Torches northern weed patches with flamethrower)",
            coOpRole2 = "Pyro Operator & Heavy Rocket (Torches south field & downs chopper)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 3500,
            respectReward = 50,
            unlockedVehiclesOrGifts = "Act II Climax: Unlocks San Fierro Garage & Doherty",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Torch 8 weed fields before timer expires", "Incinerate plots 1 through 4", "Incinerate plots 5 through 8"),
                MissionCheckpoint("cp2", "Meet Truth with Rocket Launcher", "Arm RPG-7 rocket launcher", "Lock onto hovering San Andreas police chopper"),
                MissionCheckpoint("cp3", "Drive Mothership camper to San Fierro", "Drive Truth across Garver Bridge", "Deliver van to Doherty Garage")
            ),
            isCompleted = false,
            isAvailable = false
        ),

        // ACT 3: SAN FIERRO
        Mission(
            id = "sf_01",
            title = "Wear Flowers in Your Hair",
            bossOrGiver = "Carl 'CJ' Johnson",
            act = CampaignAct.ACT_3,
            synopsis = "Recruit an all-star crew in San Fierro for the newly acquired Doherty garage: mechanics Jethro and Dwaine, and electronics wizard Zero.",
            coOpRole1 = "City Navigator (Tour through San Fierro landmarks)",
            coOpRole2 = "Crew Coordinator (Interacts with contacts at tram stations)",
            difficulty = MissionDifficulty.EASY,
            cashReward = 1000,
            respectReward = 20,
            unlockedVehiclesOrGifts = "Doherty Chop Shop Mod Garage",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Pick up Jethro at Easter Basin gas station", "Drive across Financial District", "Coordinate meeting at dock front"),
                MissionCheckpoint("cp2", "Find Dwaine at Ocean Flats hotdog van", "Navigate scenic coastline", "Convince Dwaine to join chop shop"),
                MissionCheckpoint("cp3", "Recruit Zero at RC electronics shop in Garcia", "Drive to Zero's RC shop", "Bring all 3 specialists back to Doherty")
            ),
            isCompleted = false,
            isAvailable = false
        ),
        Mission(
            id = "sf_02",
            title = "Pier 69",
            bossOrGiver = "Wu Zi Mu 'Woozie'",
            act = CampaignAct.ACT_3,
            synopsis = "The Loco Syndicate meets Ballas on Pier 69. Woozie's Triads provide rooftop sniper support. Take revenge on T-Bone Mendez and eliminate Ryder in a high-speed speedboat pursuit.",
            coOpRole1 = "Rooftop Sniper (Clears Ballas sentries on pier balustrade)",
            coOpRole2 = "Assault Breacher & Boat Pursuer (Drives speedboat & finishes Ryder)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 15000,
            respectReward = 85,
            unlockedVehiclesOrGifts = "Speedboats unlocked at San Fierro Marina",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Rooftop sniper overwatch with Triads", "Snipe Ballas guards on Pier 69 rooftop", "Cover Triad assault team below"),
                MissionCheckpoint("cp2", "Assault Pier 69 and eliminate T-Bone Mendez", "Push onto pier decking with shotguns", "Corner and execute T-Bone on balcony"),
                MissionCheckpoint("cp3", "Pursue Ryder into the bay in Speeders", "Pilot high-speed Speeder across bay", "Blow up Ryder's boat near Garver Bridge")
            ),
            isCompleted = false,
            isAvailable = false
        ),
        Mission(
            id = "sf_03",
            title = "The Da Nang Thang",
            bossOrGiver = "Wu Zi Mu 'Woozie'",
            act = CampaignAct.ACT_3,
            synopsis = "Assault the Da Nang Boys cargo freighter holding refugees. Crash-land on the deck, clear the container maze, liberate the refugees, and duel Snakehead in a katana swordfight.",
            coOpRole1 = "Katana Swordsman & Breacher (Clears ship container hold)",
            coOpRole2 = "Tactical Shotgunner (Neutralizes guards with RPGs on cranes)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 15000,
            respectReward = 100,
            unlockedVehiclesOrGifts = "Katana & Combat Shotgun at Doherty Garage",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Helicopter crash onto cargo ship deck", "Survive crash and recover dropped weapons", "Take cover behind blue shipping crates"),
                MissionCheckpoint("cp2", "Breach the dark cargo hold", "Free the locked refugee containers", "Defend refugees from Da Nang guards"),
                MissionCheckpoint("cp3", "Duel Snakehead on the bridge", "Engage Snakehead in Katana duel on command deck", "Hold bridge controls")
            ),
            isCompleted = false,
            isAvailable = false
        ),

        // ACT 4: BONE COUNTY & DESERT
        Mission(
            id = "bc_01",
            title = "Black Project",
            bossOrGiver = "The Truth",
            act = CampaignAct.ACT_4,
            synopsis = "The legendary stealth infiltration of Area 69 military research facility. Breach blast doors or vent shafts, bypass military searchlights, disable SAM missile sites, and steal the $60,000,000 Jetpack.",
            coOpRole1 = "Jetpack Pilot & Lead Infiltrator (Flies stolen Jetpack)",
            coOpRole2 = "Tactical Tech & SAM Saboteur (Disables Patriot missile defense)",
            difficulty = MissionDifficulty.EXTREME,
            cashReward = 0,
            respectReward = 120,
            unlockedVehiclesOrGifts = "THE JETPACK ($60M prototype) at Verdant Meadows",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Infiltrate perimeter before 5:30 AM", "Shoot out searchlight towers with sniper", "Crawl through ventilation shaft grates"),
                MissionCheckpoint("cp2", "Disable SAM surface-to-air missile defenses", "Locate underground control terminal", "Override SAM radar dish controls"),
                MissionCheckpoint("cp3", "Steal Jetpack from Research Laboratory", "Equip Jetpack in lower missile silo", "Fly vertically out of the silo to rendezvous point")
            ),
            isCompleted = false,
            isAvailable = false
        ),
        Mission(
            id = "bc_02",
            title = "Green Goo",
            bossOrGiver = "The Truth",
            act = CampaignAct.ACT_4,
            synopsis = "Use the freshly stolen Jetpack to intercept a heavily guarded military freight train chugging across Bone County desert. Shoot military crates to recover alien artifact 'Green Goo'.",
            coOpRole1 = "Aerial Strafer (Flies Jetpack, hovers over train cars)",
            coOpRole2 = "Train Hopper (Boards train, destroys crates with Micro SMG)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 20000,
            respectReward = 70,
            unlockedVehiclesOrGifts = "Verdant Meadows generates $10,000 daily asset revenue",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Intercept military train near Arco del Oeste", "Hover over locomotive with Jetpack", "Land on flatbed car behind armed guards"),
                MissionCheckpoint("cp2", "Shoot open three sealed military crates", "Suppress military snipers on train roofs", "Locate glowing alien canister"),
                MissionCheckpoint("cp3", "Deliver Green Goo to Truth at Airstrip", "Airlift specimen back to Verdant Meadows", "Land on airstrip hangar marker")
            ),
            isCompleted = false,
            isAvailable = false
        ),

        // ACT 5: LAS VENTURAS
        Mission(
            id = "lv_01",
            title = "Breaking the Bank at Caligula's",
            bossOrGiver = "Wu Zi Mu 'Woozie'",
            act = CampaignAct.ACT_5,
            synopsis = "The Ocean's Eleven casino heist! Infiltrate Caligula's Palace in croupier uniform, deploy gas in ventilation, blow the vault doors, steal millions in mafia casino cash, and parachute to safety.",
            coOpRole1 = "Vault Cracker & Croupier (Operates forklift & satchel charges)",
            coOpRole2 = "Armored Escort & Parachutist (Carries duffel bags & covers getaway)",
            difficulty = MissionDifficulty.EXTREME,
            cashReward = 100000,
            respectReward = 150,
            unlockedVehiclesOrGifts = "Four Dragons Casino Asset ($10,000 daily)",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Infiltrate service elevators in staff disguise", "Swipe keycard into security office", "Drop smoke canister into air ducts"),
                MissionCheckpoint("cp2", "Breach the underground casino vault", "Lift security gate with forklift", "Detonate Satchel Charges on titanium vault door"),
                MissionCheckpoint("cp3", "Escape to rooftop and base-jump to Strip", "Fight through mafia guards in service stairs", "Parachute from Caligula's roof onto airstrip van")
            ),
            isCompleted = false,
            isAvailable = false
        ),

        // ACT 6: RETURN TO LOS SANTOS
        Mission(
            id = "ls_end_01",
            title = "Los Desperados",
            bossOrGiver = "Cesar Vialpando",
            act = CampaignAct.ACT_6,
            synopsis = "Assist Cesar and the Varrios Los Aztecas in reclaiming their territory in El Corona from the Vagos during the citywide Los Santos riots.",
            coOpRole1 = "Grove Families Squad Leader (Directs street assault)",
            coOpRole2 = "Heavy Rocket Specialist (Neutralizes Vagos on rooftops)",
            difficulty = MissionDifficulty.HARD,
            cashReward = 0,
            respectReward = 90,
            unlockedVehiclesOrGifts = "El Corona and Unity Station reclaimed",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Recruit two Grove Street homies in Ganton", "Recruit homies with Desert Eagles", "Arm squad with SMGs"),
                MissionCheckpoint("cp2", "Clear El Corona residential alleys", "Push down burning alleyway", "Take out Vagos RPG operators on houses"),
                MissionCheckpoint("cp3", "Retake Cesar's house and garage", "Breach Cesar's courtyard", "Eliminate final Vagos flamethrower squad")
            ),
            isCompleted = false,
            isAvailable = false
        ),
        Mission(
            id = "ls_end_02",
            title = "End of the Line (Campaign Climax)",
            bossOrGiver = "Sean 'Sweet' Johnson",
            act = CampaignAct.ACT_6,
            synopsis = "The final battle. Ram the SWAT Tank through the Crack Palace entrance. Fight through 4 floors of drug operations. Duel Big Smoke under bulletproof vest, then escape the burning fortress before Tenpenny crashes the firetruck.",
            coOpRole1 = "Carl 'CJ' Johnson (SWAT Tank rammer, Smoke dueler & firetruck chaser)",
            coOpRole2 = "Co-Op Enforcer (Floor breacher, Night Vision sniper & gunner on firetruck)",
            difficulty = MissionDifficulty.EXTREME,
            cashReward = 250000,
            respectReward = 250,
            unlockedVehiclesOrGifts = "100% Campaign Completion & Grove Street Dominance",
            checkpoints = listOf(
                MissionCheckpoint("cp1", "Ram SWAT Tank into Crack Palace entrance", "Smash reinforced security wall", "Eliminate guard barricades with tank turret"),
                MissionCheckpoint("cp2", "Clear 4 floors of drug factory in blackout", "Equip Night Vision goggles", "Disable backup generators and sweep laboratory"),
                MissionCheckpoint("cp3", "Duel with Big Smoke on the top floor", "Corner Big Smoke behind armor plating", "Avoid incoming Ballas and Vagos reinforcements"),
                MissionCheckpoint("cp4", "Escape burning palace & Firetruck Chase", "Guide Sweet through collapsing flaming stairwells", "Hang from firetruck ladder and shoot pursuing cop cars")
            ),
            isCompleted = false,
            isAvailable = false
        )
    )

    fun getMissionsByAct(act: CampaignAct): List<Mission> {
        return allMissions.filter { it.act == act }
    }

    fun getMissionById(id: String): Mission? {
        return allMissions.find { it.id == id }
    }
}
