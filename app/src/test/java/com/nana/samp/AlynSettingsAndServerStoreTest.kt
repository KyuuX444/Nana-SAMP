package com.nana.samp

import com.nana.samp.core.AlynClientSettings
import com.nana.samp.core.AlynSettingsManager
import com.nana.samp.core.ServerStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AlynSettingsAndServerStoreTest {

    @Test
    fun parsesAuthenticAlynSettingsJson() {
        val raw = """
            {
              "client" : {
                "server" : {
                  "host" : "131.196.197.23",
                  "port" : 7777,
                  "password" : "secret"
                },
                "settings" : {
                  "nick_name" : "Vitor_X",
                  "new_interface" : true,
                  "timestamp" : true,
                  "display_fps" : true,
                  "cleo_aml" : true,
                  "fps_limit" : 60,
                  "chat_strings" : 5
                }
              }
            }
        """.trimIndent()

        val parsed = AlynSettingsManager.parseSettingsJson(raw)
        assertEquals("131.196.197.23", parsed.host)
        assertEquals(7777, parsed.port)
        assertEquals("secret", parsed.password)
        assertEquals("Vitor_X", parsed.nickname)
        assertTrue(parsed.amlEnabled)
        assertTrue(parsed.timestamp)
        assertTrue(parsed.displayFps)
        assertEquals(60, parsed.fpsLimit)
        assertEquals(5, parsed.chatLines)

        val roundTrip = AlynSettingsManager.parseSettingsJson(AlynSettingsManager.serializeSettingsJson(parsed))
        assertEquals(parsed, roundTrip)
    }

    @Test
    fun parsesDirectConnectAddressesAndSanitizesNicknames() {
        assertEquals(Pair("play.nana.id", 7777), ServerStore.parseAddress("play.nana.id"))
        assertEquals(Pair("127.0.0.1", 7778), ServerStore.parseAddress("127.0.0.1:7778"))
        assertNull(ServerStore.parseAddress(""))
        assertNull(ServerStore.parseAddress("127.0.0.1:99999"))

        assertEquals("Nana_Player[01]", ServerStore.sanitizeNickname("  Nana_Player[01]  "))
        assertEquals("NanaPlayer", ServerStore.sanitizeNickname("   "))
    }

    @Test
    fun serializesAndParsesServersAndNicknameOutsideFiles() {
        val servers = listOf(
            com.nana.samp.core.SavedServer(
                id = "127.0.0.1:7777",
                label = "Local Server",
                host = "127.0.0.1",
                port = 7777,
                password = "pass",
            )
        )
        val serialized = ServerStore.serializeServersJson(servers, "Kyuu_User")
        val (parsedServers, parsedNick) = ServerStore.parseServersAndNickname(serialized)
        assertEquals("Kyuu_User", parsedNick)
        assertEquals(1, parsedServers.size)
        assertEquals("127.0.0.1", parsedServers[0].host)
        assertEquals(7777, parsedServers[0].port)
        assertEquals("pass", parsedServers[0].password)

        // Legacy array parsing backward compatibility
        val legacyJson = """[{"id":"1.1.1.1:7777","label":"Test","host":"1.1.1.1","port":7777}]"""
        val (legacyServers, legacyNick) = ServerStore.parseServersAndNickname(legacyJson)
        assertEquals("", legacyNick)
        assertEquals(1, legacyServers.size)
        assertEquals("1.1.1.1", legacyServers[0].host)
    }
}
