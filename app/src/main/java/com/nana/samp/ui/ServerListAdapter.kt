package com.nana.samp.ui

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.nana.samp.R
import com.nana.samp.core.NativeBridge
import com.nana.samp.core.SavedServer

class ServerListAdapter(
    private val servers: List<SavedServer>,
    private val onConnect: (SavedServer) -> Unit,
    private val onDelete: (SavedServer) -> Unit,
) : RecyclerView.Adapter<ServerListAdapter.Holder>() {

    class Holder(view: View) : RecyclerView.ViewHolder(view) {
        val dot: View = view.findViewById(R.id.viewStatusDot)
        val title: TextView = view.findViewById(R.id.tvServerTitle)
        val subtitle: TextView = view.findViewById(R.id.tvServerSubtitle)
        val meta: TextView = view.findViewById(R.id.tvServerMeta)
        val proto: TextView = view.findViewById(R.id.tvServerProto)
        val btnDelete: ImageButton = view.findViewById(R.id.btnDeleteServer)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
        val v = LayoutInflater.from(parent.context).inflate(R.layout.item_server, parent, false)
        return Holder(v)
    }

    override fun getItemCount(): Int = servers.size

    override fun onBindViewHolder(holder: Holder, position: Int) {
        val s = servers[position]
        val dotRes = when (s.online) {
            true -> R.drawable.dot_online
            false -> R.drawable.dot_offline
            null -> R.drawable.dot_querying
        }
        holder.dot.setBackgroundResource(dotRes)
        holder.title.text = s.hostname.ifBlank { s.label }
        val mode = s.gamemode.ifBlank { if (s.online == false) "Offline / Unreachable" else "Querying..." }
        holder.subtitle.text = "${s.host}:${s.port}  •  $mode"
        holder.meta.text = if (s.online == true) {
            val lock = if (s.hasPassword) " [LOCK]" else ""
            "${s.players}/${s.maxPlayers}  •  ${s.pingMs} ms$lock"
        } else {
            "—"
        }
        holder.proto.text = if (s.protocolVersion == NativeBridge.PROTOCOL_03DL) "0.3.DL" else "0.3.7"
        holder.itemView.setOnClickListener { onConnect(s) }
        holder.btnDelete.setOnClickListener { onDelete(s) }
    }
}
