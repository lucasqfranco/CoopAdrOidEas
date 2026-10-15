package com.example.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Flag
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.example.data.CampaignMissionRepository
import com.example.model.CampaignAct
import com.example.model.Mission
import com.example.model.MissionDifficulty
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed
import com.example.viewmodel.CoopMainViewModel

@Composable
fun CampaignScreen(
    viewModel: CoopMainViewModel,
    modifier: Modifier = Modifier
) {
    val selectedAct by viewModel.selectedAct.collectAsState()
    val selectedDetail by viewModel.selectedMissionDetail.collectAsState()
    val session by viewModel.sessionState.collectAsState()

    val currentActMissions = CampaignMissionRepository.getMissionsByAct(selectedAct)

    Column(
        modifier = modifier
            .fillMaxSize()
            .testTag("campaign_screen")
    ) {
        // Acts Horizontal Scroll Selector
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            CampaignAct.values().forEach { act ->
                val isSelected = act == selectedAct
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(if (isSelected) GroveGreen else DarkSurfaceElevated)
                        .border(
                            1.dp,
                            if (isSelected) GroveGreen else DarkBorder,
                            RoundedCornerShape(12.dp)
                        )
                        .clickable { viewModel.setSelectedAct(act) }
                        .padding(horizontal = 14.dp, vertical = 10.dp)
                        .testTag("act_tab_${act.name}")
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(text = act.icon, fontSize = 16.sp)
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = act.title,
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 15.sp,
                            color = if (isSelected) Color.Black else Color.White
                        )
                    }
                }
            }
        }

        // Act Subtitle header
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp)
        ) {
            Text(
                text = selectedAct.subtitle,
                style = MaterialTheme.typography.bodyMedium,
                color = SanAndreasGold
            )
        }

        // Missions List
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            items(currentActMissions) { mission ->
                MissionCard(
                    mission = mission,
                    isActiveSessionMission = session.currentMission.id == mission.id,
                    onSelectMission = { viewModel.chooseMissionForSession(mission) },
                    onViewDetails = { viewModel.openMissionDetail(mission) }
                )
            }
            item {
                Spacer(modifier = Modifier.height(20.dp))
            }
        }
    }

    // Mission Checkpoints Detail Dialog
    selectedDetail?.let { mission ->
        Dialog(onDismissRequest = { viewModel.closeMissionDetail() }) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(20.dp))
                    .border(1.dp, SanAndreasGold, RoundedCornerShape(20.dp)),
                color = DarkSurfaceElevated
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = mission.title,
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 24.sp,
                            color = Color.White
                        )
                        IconButton(onClick = { viewModel.closeMissionDetail() }) {
                            Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = "Close",
                                tint = Color.LightGray
                            )
                        }
                    }

                    Text(
                        text = "Encargado: ${mission.bossOrGiver} • ${mission.act.title}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = GroveGreen
                    )

                    Spacer(modifier = Modifier.height(10.dp))

                    Text(
                        text = mission.synopsis,
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color(0xFFD1D5DB)
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    Text(
                        text = "CHECKPOINTS CO-OP & TAREAS:",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = SanAndreasGold
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        mission.checkpoints.forEachIndexed { index, cp ->
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(10.dp))
                                    .background(Color(0xFF101B13))
                                    .padding(10.dp)
                            ) {
                                Column {
                                    Text(
                                        text = "${index + 1}. ${cp.description}",
                                        style = MaterialTheme.typography.titleMedium,
                                        color = Color.White
                                    )
                                    Spacer(modifier = Modifier.height(2.dp))
                                    Text(
                                        text = "🎮 JUGADOR 1: ${cp.role1Task}",
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = GroveGreen
                                    )
                                    Text(
                                        text = "🎯 JUGADOR 2: ${cp.role2Task}",
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = SanAndreasGold
                                    )
                                }
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    Button(
                        onClick = { viewModel.chooseMissionForSession(mission) },
                        colors = ButtonDefaults.buttonColors(containerColor = GroveGreen),
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text(
                            text = "SELECCIONAR PARA LA SALA CO-OP",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 17.sp,
                            color = Color.Black
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun MissionCard(
    mission: Mission,
    isActiveSessionMission: Boolean,
    onSelectMission: () -> Unit,
    onViewDetails: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("mission_item_${mission.id}"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            if (isActiveSessionMission) SanAndreasGold else DarkBorder
        )
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = mission.title,
                        style = MaterialTheme.typography.headlineLarge,
                        color = Color.White
                    )
                    Text(
                        text = "Encargado: ${mission.bossOrGiver}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = GroveGreen
                    )
                }

                DifficultyBadge(mission.difficulty)
            }

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = mission.synopsis,
                style = MaterialTheme.typography.bodyMedium,
                color = Color(0xFFD1D5DB)
            )

            Spacer(modifier = Modifier.height(12.dp))

            // Co-op Roles Box
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color(0xFF101C13))
                    .padding(horizontal = 10.dp, vertical = 8.dp)
            ) {
                Column {
                    Text(
                        text = "P1: ${mission.coOpRole1}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color(0xFFA7F3D0)
                    )
                    Text(
                        text = "P2: ${mission.coOpRole2}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color(0xFFFDE68A)
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Rewards Row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "$${mission.cashReward}",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = SanAndreasGold
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = "+${mission.respectReward}% RESPECT",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = GroveGreen
                    )
                }

                Text(
                    text = "${mission.checkpoints.size} Checkpoints",
                    style = MaterialTheme.typography.bodyMedium,
                    color = Color.Gray
                )
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Action Buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = onViewDetails,
                    modifier = Modifier.weight(1f).height(42.dp),
                    shape = RoundedCornerShape(8.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
                ) {
                    Icon(
                        imageVector = Icons.Default.Info,
                        contentDescription = null,
                        tint = Color.LightGray,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text("CHECKPOINTS", fontFamily = BebasNeueFontFamily, color = Color.White)
                }

                Button(
                    onClick = onSelectMission,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isActiveSessionMission) SanAndreasGold else GroveGreen,
                        contentColor = Color.Black
                    ),
                    modifier = Modifier.weight(1.3f).height(42.dp),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text(
                        text = if (isActiveSessionMission) "SELECCIONADA" else "ACTIVAR EN SALA",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 15.sp
                    )
                }
            }
        }
    }
}

@Composable
private fun DifficultyBadge(difficulty: MissionDifficulty) {
    val (color, label) = when (difficulty) {
        MissionDifficulty.EASY -> GroveGreen to "FÁCIL"
        MissionDifficulty.MEDIUM -> SanAndreasGold to "MEDIA"
        MissionDifficulty.HARD -> Color(0xFFF97316) to "DIFÍCIL"
        MissionDifficulty.EXTREME -> WantedRed to "EXTREMA"
    }

    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(color.copy(alpha = 0.2f))
            .padding(horizontal = 8.dp, vertical = 3.dp)
    ) {
        Text(
            text = label,
            fontFamily = BebasNeueFontFamily,
            fontSize = 12.sp,
            color = color
        )
    }
}
