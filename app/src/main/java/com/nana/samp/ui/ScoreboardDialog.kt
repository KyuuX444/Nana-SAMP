package com.nana.samp.ui

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import com.nana.samp.core.NativeBridge

class ScoreboardDialog(
    private val activity: Activity,
    private val onClickPlayer: (Int) -> Unit,
    private val onRefresh: () -> Unit,
) {
    fun show(serverName: String, entries: List<NativeBridge.ScoreboardItem>) {
        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 20, 32, 12)
        }

        val header = TextView(activity).apply {
            text = "ID      Player Name                     Score     Ping"
            textSize = 12f
            setTypeface(Typeface.MONOSPACE, Typeface.BOLD)
            setTextColor(Color.parseColor("#38BDF8"))
            setPadding(8, 0, 8, 12)
        }
        root.addView(header)

        val rows = entries.map { e ->
            val mark = if (e.isLocal) "*" else " "
            String.format("%-4d %s  %-28s  %6d   %4d ms", e.id, mark, e.name.take(28), e.score, e.ping)
        }

        val listView = ListView(activity).apply {
            adapter = object : ArrayAdapter<String>(activity, android.R.layout.simple_list_item_1, rows) {
                override fun getView(position: Int, convertView: android.view.View?, parent: ViewGroup): android.view.View {
                    val v = super.getView(position, convertView, parent) as TextView
                    v.typeface = Typeface.MONOSPACE
                    v.textSize = 12f
                    val item = entries.getOrNull(position)
                    v.setTextColor(
                        if (item != null) ChatColorParser.sampRgbaToArgb(item.rgba) else Color.WHITE
                    )
                    return v
                }
            }
            setOnItemClickListener { _, _, position, _ ->
                entries.getOrNull(position)?.let { onClickPlayer(it.id) }
            }
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 460)
        }
        root.addView(listView)

        AlertDialog.Builder(activity)
            .setTitle("${serverName.ifBlank { "Scoreboard" }} (${entries.size} players)")
            .setView(root)
            .setNeutralButton("Refresh") { _, _ -> onRefresh() }
            .setPositiveButton("Close", null)
            .show()
    }
}
