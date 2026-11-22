package com.nana.samp.ui

import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.nana.samp.R
import com.nana.samp.core.NativeBridge

class ChatAdapter : RecyclerView.Adapter<ChatAdapter.Holder>() {

    private val items = ArrayList<NativeBridge.ChatLine>()

    class Holder(view: View) : RecyclerView.ViewHolder(view) {
        val text: TextView = view.findViewById(R.id.tvChatLine)
    }

    fun addMessage(rgba: Int, text: String) {
        addLines(listOf(NativeBridge.ChatLine(rgba, text)))
    }

    fun addLines(lines: List<NativeBridge.ChatLine>) {
        if (lines.isEmpty()) return
        val start = items.size
        items.addAll(lines)
        while (items.size > 120) {
            items.removeAt(0)
        }
        if (items.size == start + lines.size) {
            notifyItemRangeInserted(start, lines.size)
        } else {
            notifyDataSetChanged()
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
        val v = LayoutInflater.from(parent.context).inflate(R.layout.item_chat_line, parent, false)
        return Holder(v)
    }

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: Holder, position: Int) {
        val line = items[position]
        val sb = SpannableStringBuilder()
        for (seg in ChatColorParser.parse(line.text, line.rgba)) {
            val s = sb.length
            sb.append(seg.text)
            sb.setSpan(ForegroundColorSpan(seg.argbColor), s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        holder.text.text = sb
    }
}
