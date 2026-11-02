package com.lsfg.android.ui

import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.School
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.NavHostController
import com.lsfg.android.R
import com.lsfg.android.ui.components.IconBadge
import com.lsfg.android.ui.components.LsfgCard
import com.lsfg.android.ui.components.LsfgTopBar
import com.lsfg.android.ui.theme.LsfgPrimary

/**
 * One step of the tutorial. Add new entries to [tutorialSteps] below as you
 * drop screenshots into res/drawable-nodpi.
 */
private data class TutorialStep(
    val title: String,
    val description: String,
    @DrawableRes val image: Int?,
)

/**
 * Tutorial entries. Replace the placeholder list with your own steps:
 *
 *   TutorialStep(
 *       title = "Step 1 — Pick the DLL",
 *       description = "Long description shown under the screenshot.",
 *       image = R.drawable.tutorial_step_1,
 *   ),
 *
 * Drop the PNG/JPG files into LSFG-Android/app/src/main/res/drawable-nodpi/
 * (any density-agnostic folder works) and reference them via R.drawable.<name>.
 * Leave [image] = null to render a text-only step.
 */
private val tutorialSteps: List<TutorialStep> = listOf(
    // TODO: aggiungi qui i tuoi step. Esempio:
    // TutorialStep(
    //     title = "1. Seleziona la DLL",
    //     description = "Apri 'Select DLL' dalla home e scegli Lossless.dll dalla tua copia di Lossless Scaling.",
    //     image = R.drawable.tutorial_step_1,
    // ),

    TutorialStep(
        title = "1. 无障碍权限 — 第 1 步",
        description = "LSFG-Android 需要无障碍权限来正确驱动悬浮层和帧生成。可直接从系统设置应用打开设备的无障碍设置，或点按 LSFG-Android 主屏幕右上角的无障碍图标。",
        image = R.drawable.tutorial_step_1,
    ),

    TutorialStep(
        title = "1. 无障碍权限 — 第 2 步",
        description = "在无障碍设置列表中，找到并点按“LSFG 触摸穿透”。",
        image = R.drawable.tutorial_step_2,
    ),

    TutorialStep(
        title = "1. 无障碍权限 — 第 3 步",
        description = "如图所示，为 LSFG-Android 打开无障碍权限开关。",
        image = R.drawable.tutorial_step_3,
    ),

    TutorialStep(
        title = "1. 无障碍权限 — 第 4 步",
        description = "如果开关无法打开，你的设备很可能拦截了侧载应用的受限设置。打开 LSFG-Android 的 Android 应用信息页，查看右上角 — 你会看到一个菜单图标（截图中红框标出）。点按它并启用出现的选项（通常是“允许受限设置”）。完成后返回并重复上一步 — 无障碍开关即可打开。",
        image = R.drawable.tutorial_step_4,
    ),

    TutorialStep(
        title = "2. AI 插帧引擎 — 第 1 步",
        description = "应用已内置 RIFE v4 AI 插帧模型，开箱即用 — 主页第 1 步的“AI 插帧引擎”始终显示就绪。如需调整插帧参数，点按该卡片即可进入“帧生成与节奏”页面。",
        image = R.drawable.tutorial_step_5,
    ),

    TutorialStep(
        title = "2. AI 插帧引擎 — 第 2 步",
        description = "法律声明页说明了本应用的授权与免责信息：本应用基于 rife-ncnn-vulkan 与 ncnn（MIT 许可）在设备本地运行推理，无需任何外部文件，也不对使用效果作保证。",
        image = R.drawable.tutorial_step_6,
    ),

    TutorialStep(
        title = "2. AI 插帧引擎 — 第 3 步",
        description = "RIFE 模型已随应用打包内置 — 无需任何文件选择或提取步骤，开箱即用。",
        image = R.drawable.tutorial_step_7,
    ),

    TutorialStep(
        title = "2. AI 插帧引擎 — 第 4 步",
        description = "一切就绪：回到主页选择目标应用，即可启动 AI 插帧会话。",
        image = R.drawable.tutorial_step_8,
    ),

    TutorialStep(
        title = "3. 选择目标应用 — 第 1 步",
        description = "要指定帧生成作用于哪个应用，请打开主页菜单第 2 部分 — “目标应用”。",
        image = R.drawable.tutorial_step_9,
    ),

    TutorialStep(
        title = "3. 选择目标应用 — 第 2 步",
        description = "你会看到设备上安装的所有应用和游戏。使用搜索栏快速找到目标，点按即可将其设为目标应用。",
        image = R.drawable.tutorial_step_10,
    ),

    TutorialStep(
        title = "4. 悬浮层与显示 — 第 1 步",
        description = "此部分包含与 LSFG 悬浮层及其屏幕显示方式相关的所有设置。",
        image = R.drawable.tutorial_step_11,
    ),

    TutorialStep(
        title = "4. 悬浮层与显示 — 第 2 步",
        description = "此设置控制如何呼出会话内的悬浮层菜单。默认为图标按钮：会话中点按即可打开实时设置抽屉。也可以切换为边缘滑入式抽屉（从屏幕边缘向中心拖动）。警告：在部分 Android 设备上抽屉模式不可靠，可能导致无法打开悬浮层菜单甚至无法关闭悬浮层。若发生此情况，可能需要重启设备以恢复图标按钮模式。请优先尝试图标按钮。",
        image = R.drawable.tutorial_step_12,
    ),

    TutorialStep(
        title = "4. 悬浮层与显示 — 第 3 步",
        description = "如果你的设备支持抽屉，可通过此设置选择抽屉所在的屏幕边缘（左、右、上或下）。",
        image = R.drawable.tutorial_step_13,
    ),

    TutorialStep(
        title = "4. 悬浮层与显示 — 第 4 步",
        description = "这里可以启用屏幕 FPS 计数器和帧节奏图表。注意：这些读数为尽力统计，可能不是 100% 准确。",
        image = R.drawable.tutorial_step_14,
    ),

    TutorialStep(
        title = "4. 悬浮层与显示 — 第 5 步",
        description = "此选项通过无障碍服务而非普通系统悬浮层来承载悬浮层。如果标准悬浮层在你的设备上表现异常，可尝试启用 — 它对激进的后台查杀更稳健。",
        image = R.drawable.tutorial_step_15,
    ),

    TutorialStep(
        title = "4. 悬浮层与显示 — 第 6 步",
        description = "在 MediaProjection 采集（默认）与 Shizuku 辅助采集之间选择。如果你已尝试过 MediaProjection — 包括启用和禁用上一步的无障碍承载悬浮层 — 悬浮层仍然不工作，可在此切换到 Shizuku 作为备选。",
        image = R.drawable.tutorial_step_16,
    ),

    TutorialStep(
        title = "5. 帧生成与节奏 — 第 1 步",
        description = "此部分汇集了控制帧生成行为的所有设置。",
        image = R.drawable.tutorial_step_17,
    ),

    TutorialStep(
        title = "5. 帧生成与节奏 — 第 2 步",
        description = "这些是我认为在骁龙 8 Elite Gen 5 等顶级设备上的最优设置 — 这类芯片上光流缩放也可以放心调到 1。即使在旗舰设备上，我也建议使用性能变体而非标准变体。抗伪影模式为实验性，不建议启用。",
        image = R.drawable.tutorial_step_18,
    ),

    TutorialStep(
        title = "6. 启动悬浮层 — 第 1 步",
        description = "一切配置完成后，点按“启动会话”以连同帧生成悬浮层一起启动目标应用。",
        image = R.drawable.tutorial_step_19,
    ),

    TutorialStep(
        title = "6. 启动悬浮层 — 第 2 步",
        description = "在部分 Android 设备上，此时可能出现屏幕共享选择器。出现时，请选择你在 LSFG-Android 中已设为目标的应用 — 而不是整个屏幕。",
        image = R.drawable.tutorial_step_20,
    ),

    TutorialStep(
        title = "6. 启动悬浮层 — 第 3 步",
        description = "目标应用将带着在其上层运行的 LSFG 悬浮层启动，帧生成随即生效。",
        image = R.drawable.tutorial_step_21,
    ),

    TutorialStep(
        title = "7. 悬浮层菜单 — 第 1 步",
        description = "如果你在设置中选择了图标按钮，会话期间它会出现在屏幕一侧 — 点按即可打开悬浮层菜单。",
        image = R.drawable.tutorial_step_22,
    ),

    TutorialStep(
        title = "7. 悬浮层菜单 — 第 2 步",
        description = "如果你选择了抽屉，它会出现在你选择的屏幕边缘。要打开悬浮层菜单，请将抽屉从边缘向屏幕中心滑动。",
        image = R.drawable.tutorial_step_23,
    ),

    TutorialStep(
        title = "7. 悬浮层菜单 — 第 3 步",
        description = "这些是悬浮层菜单中的全部选项 — 此处列出的每个参数都可在会话中实时调整。",
        image = R.drawable.tutorial_step_24,
    ),
    TutorialStep(
        title = "7. 悬浮层菜单 — 第 4 步",
        description = "",
        image = R.drawable.tutorial_step_25,
    ),
    TutorialStep(
        title = "7. 悬浮层菜单 — 第 5 步",
        description = "",
        image = R.drawable.tutorial_step_26,
    ),
    TutorialStep(
        title = "7. 悬浮层菜单 — 第 6 步",
        description = "",
        image = R.drawable.tutorial_step_27,
    ),
)

@Composable
fun TutorialScreen(nav: NavHostController) {
    var zoomedImage by remember { mutableStateOf<Int?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .padding(horizontal = 20.dp)
            .padding(bottom = 20.dp),
    ) {
        LsfgTopBar(
            title = stringResource(R.string.nav_tutorial),
            onBack = { nav.popBackStack() },
        )

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 12.dp, bottom = 4.dp),
            ) {
                IconBadge(icon = Icons.Filled.School, tint = LsfgPrimary, size = 56.dp)
            }

            LsfgCard(accent = true) {
                Text(
                    text = stringResource(R.string.tutorial_subtitle).uppercase(),
                    style = MaterialTheme.typography.labelSmall,
                    color = LsfgPrimary,
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    text = stringResource(R.string.tutorial_intro),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }

            if (tutorialSteps.isEmpty()) {
                LsfgCard {
                    Text(
                        text = stringResource(R.string.tutorial_empty),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            } else {
                tutorialSteps.forEachIndexed { index, step ->
                    TutorialStepCard(
                        index = index + 1,
                        step = step,
                        onImageClick = { res -> zoomedImage = res },
                    )
                }
            }

            Spacer(Modifier.height(8.dp))
        }
    }

    zoomedImage?.let { res ->
        Dialog(
            onDismissRequest = { zoomedImage = null },
            properties = DialogProperties(usePlatformDefaultWidth = false),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .clickable { zoomedImage = null },
                contentAlignment = Alignment.Center,
            ) {
                Image(
                    painter = painterResource(id = res),
                    contentDescription = null,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    contentScale = ContentScale.Fit,
                )
                Box(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(20.dp)
                        .size(40.dp)
                        .clip(RoundedCornerShape(20.dp))
                        .clickable { zoomedImage = null },
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        imageVector = Icons.Filled.Close,
                        contentDescription = "Close",
                        tint = Color.White,
                    )
                }
            }
        }
    }
}

@Composable
private fun TutorialStepCard(
    index: Int,
    step: TutorialStep,
    onImageClick: (Int) -> Unit,
) {
    LsfgCard {
        Text(
            text = "步骤 ${index.toString().padStart(2, '0')}",
            style = MaterialTheme.typography.labelSmall,
            color = LsfgPrimary,
        )
        Spacer(Modifier.height(6.dp))
        Text(
            text = step.title,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onSurface,
        )
        if (step.image != null) {
            Spacer(Modifier.height(12.dp))
            Image(
                painter = painterResource(id = step.image),
                contentDescription = step.title,
                contentScale = ContentScale.Fit,
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(16f / 9f)
                    .clip(RoundedCornerShape(12.dp))
                    .clickable { onImageClick(step.image) },
            )
        }
        if (step.description.isNotBlank()) {
            Spacer(Modifier.height(12.dp))
            Text(
                text = step.description,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}
