package com.example

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Flag
import androidx.compose.material.icons.filled.Group
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.PictureInPicture
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.example.ui.components.MissionPassBanner
import com.example.ui.screens.ActiveMissionOverlay
import com.example.ui.screens.CampaignScreen
import com.example.ui.screens.InstallationScreen
import com.example.ui.screens.LobbyScreen
import com.example.ui.screens.VoiceChatScreen
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkSurfaceBase
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.MyApplicationTheme
import com.example.ui.theme.SanAndreasGold
import com.example.viewmodel.AppNavTab
import com.example.viewmodel.CoopMainViewModel

class MainActivity : ComponentActivity() {

    private val viewModel: CoopMainViewModel by viewModels()

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            MyApplicationTheme {
                val currentTab by viewModel.currentTab.collectAsState()
                val session by viewModel.sessionState.collectAsState()
                val voiceState by viewModel.voiceState.collectAsState()
                val isOverlayActive by viewModel.isOverlayModeActive.collectAsState()
                val showPassBanner by viewModel.showPassBanner.collectAsState()
                val bannerRespect by viewModel.bannerRespect.collectAsState()
                val bannerCash by viewModel.bannerCash.collectAsState()
                val notificationMsg by viewModel.notificationMessage.collectAsState()

                val snackbarHostState = remember { SnackbarHostState() }

                val permissionLauncher = rememberLauncherForActivityResult(
                    contract = ActivityResultContracts.RequestPermission()
                ) { isGranted ->
                    viewModel.setAudioPermissionGranted(isGranted)
                }

                LaunchedEffect(Unit) {
                    val hasPerm = ContextCompat.checkSelfPermission(
                        this@MainActivity,
                        Manifest.permission.RECORD_AUDIO
                    ) == PackageManager.PERMISSION_GRANTED
                    if (hasPerm) {
                        viewModel.setAudioPermissionGranted(true)
                    } else {
                        permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                    }
                }

                LaunchedEffect(notificationMsg) {
                    notificationMsg?.let { msg ->
                        snackbarHostState.showSnackbar(msg)
                        viewModel.clearNotification()
                    }
                }

                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    topBar = {
                        TopAppBar(
                            title = {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Text(
                                        text = "COOPANDREAS",
                                        fontFamily = BebasNeueFontFamily,
                                        fontSize = 24.sp,
                                        color = GroveGreen,
                                        letterSpacing = 1.sp
                                    )
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Box(
                                        modifier = Modifier
                                            .clip(RoundedCornerShape(6.dp))
                                            .background(SanAndreasGold.copy(alpha = 0.2f))
                                            .padding(horizontal = 6.dp, vertical = 2.dp)
                                    ) {
                                        Text(
                                            text = session.roomCode,
                                            fontFamily = BebasNeueFontFamily,
                                            fontSize = 12.sp,
                                            color = SanAndreasGold
                                        )
                                    }
                                }
                            },
                            actions = {
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(8.dp))
                                        .background(
                                            if (voiceState.isPttPressed) GroveGreen.copy(alpha = 0.25f)
                                            else Color(0xFF142418)
                                        )
                                        .padding(horizontal = 8.dp, vertical = 4.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Row(verticalAlignment = Alignment.CenterVertically) {
                                        Box(
                                            modifier = Modifier
                                                .size(6.dp)
                                                .clip(CircleShape)
                                                .background(if (voiceState.isPttPressed) GroveGreen else Color.Gray)
                                        )
                                        Spacer(modifier = Modifier.width(4.dp))
                                        Text(
                                            text = if (voiceState.isPttPressed) "HABLANDO" else "RADIO",
                                            fontFamily = BebasNeueFontFamily,
                                            fontSize = 11.sp,
                                            color = if (voiceState.isPttPressed) GroveGreen else Color.LightGray
                                        )
                                    }
                                }

                                IconButton(
                                    onClick = { viewModel.toggleOverlayMode() },
                                    modifier = Modifier.testTag("toggle_overlay_action")
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.PictureInPicture,
                                        contentDescription = "Companion Overlay",
                                        tint = if (isOverlayActive) GroveGreen else Color.LightGray
                                    )
                                }
                            },
                            colors = TopAppBarDefaults.topAppBarColors(
                                containerColor = DarkSurfaceBase,
                                titleContentColor = Color.White
                            )
                        )
                    },
                    bottomBar = {
                        NavigationBar(
                            containerColor = DarkSurfaceElevated,
                            contentColor = Color.White,
                            modifier = Modifier.testTag("bottom_nav_bar")
                        ) {
                            NavigationBarItem(
                                selected = currentTab == AppNavTab.LOBBY,
                                onClick = { viewModel.setNavTab(AppNavTab.LOBBY) },
                                icon = {
                                    Icon(Icons.Default.Group, contentDescription = "Lobby Co-op")
                                },
                                label = {
                                    Text("Lobby Co-op", fontFamily = BebasNeueFontFamily, fontSize = 13.sp)
                                },
                                modifier = Modifier.testTag("nav_tab_lobby"),
                                colors = NavigationBarItemDefaults.colors(
                                    selectedIconColor = Color.Black,
                                    selectedTextColor = GroveGreen,
                                    indicatorColor = GroveGreen,
                                    unselectedIconColor = Color.Gray,
                                    unselectedTextColor = Color.Gray
                                )
                            )

                            NavigationBarItem(
                                selected = currentTab == AppNavTab.CAMPAIGN,
                                onClick = { viewModel.setNavTab(AppNavTab.CAMPAIGN) },
                                icon = {
                                    Icon(Icons.Default.Flag, contentDescription = "Campaña")
                                },
                                label = {
                                    Text("Campaña", fontFamily = BebasNeueFontFamily, fontSize = 13.sp)
                                },
                                modifier = Modifier.testTag("nav_tab_campaign"),
                                colors = NavigationBarItemDefaults.colors(
                                    selectedIconColor = Color.Black,
                                    selectedTextColor = GroveGreen,
                                    indicatorColor = GroveGreen,
                                    unselectedIconColor = Color.Gray,
                                    unselectedTextColor = Color.Gray
                                )
                            )

                            NavigationBarItem(
                                selected = currentTab == AppNavTab.VOICE,
                                onClick = { viewModel.setNavTab(AppNavTab.VOICE) },
                                icon = {
                                    Icon(Icons.Default.Mic, contentDescription = "Radio Comms")
                                },
                                label = {
                                    Text("Radio Comms", fontFamily = BebasNeueFontFamily, fontSize = 13.sp)
                                },
                                modifier = Modifier.testTag("nav_tab_voice"),
                                colors = NavigationBarItemDefaults.colors(
                                    selectedIconColor = Color.Black,
                                    selectedTextColor = GroveGreen,
                                    indicatorColor = GroveGreen,
                                    unselectedIconColor = Color.Gray,
                                    unselectedTextColor = Color.Gray
                                )
                            )

                            NavigationBarItem(
                                selected = currentTab == AppNavTab.INSTALLATION,
                                onClick = { viewModel.setNavTab(AppNavTab.INSTALLATION) },
                                icon = {
                                    Icon(Icons.Default.Download, contentDescription = "Instalación")
                                },
                                label = {
                                    Text("Instalación", fontFamily = BebasNeueFontFamily, fontSize = 13.sp)
                                },
                                modifier = Modifier.testTag("nav_tab_installation"),
                                colors = NavigationBarItemDefaults.colors(
                                    selectedIconColor = Color.Black,
                                    selectedTextColor = GroveGreen,
                                    indicatorColor = GroveGreen,
                                    unselectedIconColor = Color.Gray,
                                    unselectedTextColor = Color.Gray
                                )
                            )
                        }
                    },
                    snackbarHost = { SnackbarHost(snackbarHostState) }
                ) { innerPadding ->
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(innerPadding)
                            .background(DarkSurfaceBase)
                    ) {
                        when (currentTab) {
                            AppNavTab.LOBBY -> LobbyScreen(viewModel = viewModel)
                            AppNavTab.CAMPAIGN -> CampaignScreen(viewModel = viewModel)
                            AppNavTab.VOICE -> VoiceChatScreen(viewModel = viewModel)
                            AppNavTab.INSTALLATION -> InstallationScreen(viewModel = viewModel)
                        }

                        AnimatedVisibility(
                            visible = isOverlayActive,
                            enter = slideInVertically(initialOffsetY = { it }) + fadeIn(),
                            exit = slideOutVertically(targetOffsetY = { it }) + fadeOut()
                        ) {
                            ActiveMissionOverlay(viewModel = viewModel)
                        }

                        AnimatedVisibility(
                            visible = showPassBanner,
                            enter = fadeIn(),
                            exit = fadeOut()
                        ) {
                            MissionPassBanner(
                                missionTitle = session.currentMission.title,
                                respectReward = bannerRespect,
                                cashReward = bannerCash,
                                onContinue = { viewModel.dismissPassBanner() }
                            )
                        }
                    }
                }
            }
        }
    }
}
