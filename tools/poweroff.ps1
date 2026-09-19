# =============================================================================
#  tools\poweroff.ps1  ——  安全关闭开发板（断电前必须走这一步）
#
#  【为什么必须这么做】
#      板子的 SD 卡是 ext4 的 **writeback** 模式：数据先写在内存里，
#      由内核找机会慢慢刷回 SD 卡。如果你直接拔电 / 直接关排插：
#          → 内存里没刷下去的数据**永久丢失**
#          → 更糟的是：正在写的文件会被**填成全零**
#      本工程已因此 **报废过两次 Git 仓库**。
#
#  【用法】
#      .\tools\poweroff.ps1
#
#  【三种"退出"的区别 —— 别再搞混了】
#
#      ┌─────────────────────────────────────────────────────────────┐
#      │ ① 我只是不想用了，板子还要接着跑                              │
#      │      →  直接输  exit  （或关掉终端窗口）                      │
#      │      →  板子**继续正常运行**，只是断开了你的 SSH 连接          │
#      │      →  ✅ 完全安全，不用管                                   │
#      ├─────────────────────────────────────────────────────────────┤
#      │ ② 我要关机 / 要断电了                                        │
#      │      →  跑本脚本，然后**等指示灯熄灭**                        │
#      │      →  ✅ 安全                                                │
#      ├─────────────────────────────────────────────────────────────┤
#      │ ③ 直接拔电源 / 直接关排插                                    │
#      │      →  🔴 **可能毁掉文件，甚至毁掉整个 Git 仓库**             │
#      └─────────────────────────────────────────────────────────────┘
#
#  ⚠️ 最危险的时刻：**刚 git commit / 刚 sync 完就拔电**。
#     因为那时对象文件刚写完，还在内存里没落盘。
#
#  NOTE: 本文件需要保存为 "UTF-8 with BOM"（PowerShell 5.1 才认中文）。
# =============================================================================

$ErrorActionPreference = "Stop"

$BOARD = "board"      # ~/.ssh/config 里的别名

Write-Host ""
Write-Host "==> 正在安全关机：sync（把内存刷到 SD 卡） + poweroff" -ForegroundColor Cyan
Write-Host "    板子会要一次 sudo 密码，输完等它自己关。" -ForegroundColor Gray
Write-Host ""

# -t 分配终端：sudo 必须有终端才能读密码
ssh -t $BOARD "sync && sudo poweroff"

Write-Host ""
Write-Host "[OK] 关机指令已下发" -ForegroundColor Green
Write-Host ""
Write-Host "  ⚠️  现在请看着板子上的指示灯 ——" -ForegroundColor Yellow
Write-Host "     **等灯熄灭之后**，才能拔电源 / 关排插。" -ForegroundColor Yellow
Write-Host "     灯还亮着就说明还在刷盘，这时候拔电就是案例③。" -ForegroundColor Yellow
Write-Host ""
Write-Host "  验证（等 10 秒左右）：" -ForegroundColor Gray
Write-Host "     ssh board `"echo still-alive`"   # 连不上就说明关掉了" -ForegroundColor Gray
Write-Host ""
