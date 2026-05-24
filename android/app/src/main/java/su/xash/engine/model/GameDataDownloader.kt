package su.xash.engine.model

import android.content.Context
import android.util.Base64
import android.util.Log
import android.os.Handler
import java.io.ByteArrayInputStream
import java.io.File
import java.util.zip.GZIPInputStream
import java.util.zip.ZipInputStream
import java.io.InputStream
import java.io.RandomAccessFile
import org.tukaani.xz.LZMAInputStream
import java.net.HttpURLConnection
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URL
import java.security.KeyFactory
import java.security.SecureRandom
import java.security.spec.RSAPublicKeySpec
import javax.crypto.Cipher
import javax.crypto.Mac
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.yield
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import java.math.BigInteger
import java.util.concurrent.atomic.AtomicLong
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

class GameDataDownloader(private val ctx: Context) {

    companion object {
        val GAMES = linkedMapOf(
            "valve" to GameInfo(90, listOf(71, 4, 1), "Half-Life", 400_000_000L),
            "cstrike" to GameInfo(90, listOf(11), "Counter-Strike", 280_000_000L),
            "tfc" to GameInfo(90, listOf(21), "Team Fortress Classic", 119_000_000L),
            "gearbox" to GameInfo(90, listOf(51), "Opposing Force", 273_000_000L),
            "czero" to GameInfo(90, listOf(81), "Condition Zero", 400_000_000L)
        )

        // Hardcoded manifest IDs for anonymous downloads (from HLDS-appmanifest repo, July 2023)
        // These may become stale; Steam may reject old manifests with 401/manifest_code=0.
        private val HARDCODED_MANIFEST_IDS = mapOf(
            71 to 9183617604528345869L,
            11 to 4720911300072406946L,
            21 to 7841127166138118042L,
            51 to 789184054796507140L,
            81 to 3601230779843470737L,
            1  to 5928322771446233610L,
            4  to 8690279432129063737L,
            1006 to 6912453647411644579L
        )

        private val nextJobId = AtomicLong(1L)

        private val CM_SERVERS = listOf(
            "162.254.197.40" to 27017,
            "162.254.199.170" to 27017,
            "155.133.248.34" to 27017,
            "155.133.248.35" to 27017,
            "155.133.246.43" to 27017,
        )

        private const val PROTO_MASK = 0x80000000.toInt()
        private val ANON_STEAM_ID = (10L shl 52) or (1L shl 56)

        private const val TCP_MAGIC = 0x31305456

        // Steam Public Universe RSA Public Key (from SteamKit2 KeyDictionary)
        // This is the hardcoded public key for Steam's public (production) servers
        private val STEAM_PUBLIC_KEY = byteArrayOf(
            0x30.toByte(), 0x81.toByte(), 0x9D.toByte(), 0x30.toByte(), 0x0D.toByte(), 0x06.toByte(), 0x09.toByte(), 0x2A.toByte(),
            0x86.toByte(), 0x48.toByte(), 0x86.toByte(), 0xF7.toByte(), 0x0D.toByte(), 0x01.toByte(), 0x01.toByte(), 0x01.toByte(),
            0x05.toByte(), 0x00.toByte(), 0x03.toByte(), 0x81.toByte(), 0x8B.toByte(), 0x00.toByte(), 0x30.toByte(), 0x81.toByte(),
            0x87.toByte(), 0x02.toByte(), 0x81.toByte(), 0x81.toByte(), 0x00.toByte(), 0xDF.toByte(), 0xEC.toByte(), 0x1A.toByte(),
            0xD6.toByte(), 0x2C.toByte(), 0x10.toByte(), 0x66.toByte(), 0x2C.toByte(), 0x17.toByte(), 0x35.toByte(), 0x3A.toByte(),
            0x14.toByte(), 0xB0.toByte(), 0x7C.toByte(), 0x59.toByte(), 0x11.toByte(), 0x7F.toByte(), 0x9D.toByte(), 0xD3.toByte(),
            0xD8.toByte(), 0x2B.toByte(), 0x7A.toByte(), 0xE3.toByte(), 0xE0.toByte(), 0x15.toByte(), 0xCD.toByte(), 0x19.toByte(),
            0x1E.toByte(), 0x46.toByte(), 0xE8.toByte(), 0x7B.toByte(), 0x87.toByte(), 0x74.toByte(), 0xA2.toByte(), 0x18.toByte(),
            0x46.toByte(), 0x31.toByte(), 0xA9.toByte(), 0x03.toByte(), 0x14.toByte(), 0x79.toByte(), 0x82.toByte(), 0x8E.toByte(),
            0xE9.toByte(), 0x45.toByte(), 0xA2.toByte(), 0x49.toByte(), 0x12.toByte(), 0xA9.toByte(), 0x23.toByte(), 0x68.toByte(),
            0x73.toByte(), 0x89.toByte(), 0xCF.toByte(), 0x69.toByte(), 0xA1.toByte(), 0xB1.toByte(), 0x61.toByte(), 0x46.toByte(),
            0xBD.toByte(), 0xC1.toByte(), 0xBE.toByte(), 0xBF.toByte(), 0xD6.toByte(), 0x01.toByte(), 0x1B.toByte(), 0xD8.toByte(),
            0x81.toByte(), 0xD4.toByte(), 0xDC.toByte(), 0x90.toByte(), 0xFB.toByte(), 0xFE.toByte(), 0x4F.toByte(), 0x52.toByte(),
            0x73.toByte(), 0x66.toByte(), 0xCB.toByte(), 0x95.toByte(), 0x70.toByte(), 0xD7.toByte(), 0xC5.toByte(), 0x8E.toByte(),
            0xBA.toByte(), 0x1C.toByte(), 0x7A.toByte(), 0x33.toByte(), 0x75.toByte(), 0xA1.toByte(), 0x62.toByte(), 0x34.toByte(),
            0x46.toByte(), 0xBB.toByte(), 0x60.toByte(), 0xB7.toByte(), 0x80.toByte(), 0x68.toByte(), 0xFA.toByte(), 0x13.toByte(),
            0xA7.toByte(), 0x7A.toByte(), 0x8A.toByte(), 0x37.toByte(), 0x4B.toByte(), 0x9E.toByte(), 0xC6.toByte(), 0xF4.toByte(),
            0x5D.toByte(), 0x5F.toByte(), 0x3A.toByte(), 0x99.toByte(), 0xF9.toByte(), 0x9E.toByte(), 0xC4.toByte(), 0x3A.toByte(),
            0xE9.toByte(), 0x63.toByte(), 0xA2.toByte(), 0xBB.toByte(), 0x88.toByte(), 0x19.toByte(), 0x28.toByte(), 0xE0.toByte(),
            0xE7.toByte(), 0x14.toByte(), 0xC0.toByte(), 0x42.toByte(), 0x89.toByte(), 0x02.toByte(), 0x01.toByte(), 0x11.toByte()
        )

        fun formatSize(bytes: Long): String {
            return when {
                bytes >= 1_000_000_000 -> "${bytes / 1_000_000_000} GB"
                bytes >= 1_000_000 -> "${bytes / 1_000_000} MB"
                bytes >= 1_000 -> "${bytes / 1_000} KB"
                else -> "$bytes B"
            }
        }

        private fun parseRSAPubKey(data: ByteArray, debugCtx: String = ""): Pair<ByteArray, ByteArray> {
            val dataHex = data.take(4).joinToString("") { "%02X".format(it) } + if (data.size > 4) "..." else ""
            val stackTrace = Thread.currentThread().stackTrace.filter { it.className.contains("GameDataDownloader") }.take(3).joinToString(" -> ") { "${it.methodName}(${it.lineNumber})" }
            if (debugCtx.isNotEmpty())
                throw Exception("parseRSAPubKey: FROM=$debugCtx data.size=${data.size} dataHex=$dataHex stack=$stackTrace")
            val expLen = data[0].toInt() and 0xFF
            val exp: ByteArray
            try {
                exp = data.copyOfRange(1, 1 + expLen)
            } catch (e: Exception) {
                throw Exception("parseRSAPubKey exp: data.size=${data.size} expLen=$expLen dataHex=$dataHex", e)
            }
            val mod: ByteArray
            try {
                mod = data.copyOfRange(1 + expLen, data.size)
            } catch (e: Exception) {
                throw Exception("parseRSAPubKey mod: data.size=${data.size} expLen=$expLen", e)
            }
            return exp to mod
        }

        internal fun buildEncryptResponse(encryptedKey: ByteArray, keyCrc: Int): ByteArray {
            val msgHdr = ByteArrayOutputStream()
            msgHdr.write(Proto.packInt32(1304))
            msgHdr.write(Proto.packInt64(-1L))
            msgHdr.write(Proto.packInt64(-1L))
            val hdrBytes = msgHdr.toByteArray()
            
            val body = ByteArrayOutputStream()
            body.write(Proto.packInt32(1))
            body.write(Proto.packInt32(encryptedKey.size))
            val bodyBytes = body.toByteArray()
            
            val payload = ByteArrayOutputStream()
            payload.write(encryptedKey)
            payload.write(Proto.packInt32(keyCrc))
            payload.write(Proto.packInt32(0))
            val payloadBytes = payload.toByteArray()
            
            val fos = ByteArrayOutputStream()
            fos.write(hdrBytes)
            fos.write(bodyBytes)
            fos.write(payloadBytes)
            return fos.toByteArray()
        }

        internal fun buildClientHello(): ByteArray {
            val buf = ByteArrayOutputStream()
            buf.write(Proto.packVarint(1 shl 3 or 0))
            buf.write(Proto.packVarint(65581))
            return buf.toByteArray()
        }

internal fun buildLogonBody(): ByteArray {
            val buf = ByteArrayOutputStream()
            Log.d("SteamCM", "[LOGON] buildLogonBody: building...")
            
            val f1 = Proto.packVarint(1 shl 3 or 0)
            val v1 = Proto.packVarint(65581)
            buf.write(f1); buf.write(v1)
            Log.d("SteamCM", "[LOGON] field 1 (protocol_version=65581) tag=${f1[0].toInt() and 0xFF} value hex=${v1.joinToString("") { "%02X".format(it) }}")
            
            buf.write(Proto.packVarint(6 shl 3 or 2))
            val lang = "english".toByteArray()
            buf.write(Proto.packVarint(lang.size)); buf.write(lang)
            Log.d("SteamCM", "[LOGON] field 6 (client_language=english) tag=32 len=${lang.size}")
            
            val f7 = Proto.packVarint(7 shl 3 or 0)
            val v7 = Proto.packVarint(-500)
            buf.write(f7); buf.write(v7)
Log.d("SteamCM", "[LOGON] field 7 (client_os_type=-500 AndroidUnknown) tag=${f7[0].toInt() and 0xFF} value hex=${v7.joinToString("") { "%02X".format(it) }}")
            
            buf.write(Proto.packVarint(30 shl 3 or 2))
            val machineId = "JavaSteam-SerialNumber".toByteArray()
            buf.write(Proto.packVarint(machineId.size)); buf.write(machineId)
            Log.d("SteamCM", "[LOGON] field 30 (machine_id) len=${machineId.size}")
            
            val result = buf.toByteArray()
            Log.d("SteamCM", "[LOGON] buildLogonBody: FINAL hex=${result.joinToString("") { "%02X".format(it) }} size=${result.size}")
            return result
        }

        internal fun buildProtoHeader(steamId: Long, sessionId: Int, targetJobName: String? = null, jobidSource: Long? = null): ByteArray {
            val buf = ByteArrayOutputStream()
            Log.d("SteamCM", "[PROTO] buildProtoHeader: steamId=$steamId sessionId=$sessionId${if (targetJobName != null) " targetJobName=$targetJobName" else ""}${if (jobidSource != null) " jobidSource=$jobidSource" else ""}")
            val tag1 = Proto.packVarint(1 shl 3 or 1)
            val val1 = Proto.packInt64(steamId)
            buf.write(tag1); buf.write(val1)
            val tag2 = Proto.packVarint(2 shl 3 or 0)
            val val2 = Proto.packVarint(sessionId)
            buf.write(tag2); buf.write(val2)
            if (jobidSource != null) {
                val tag10 = Proto.packVarint(10 shl 3 or 1)
                val val10 = Proto.packInt64(jobidSource)
                buf.write(tag10); buf.write(val10)
            }
            if (targetJobName != null) {
                val tag12 = Proto.packVarint(12 shl 3 or 2)
                val nameBytes = targetJobName.toByteArray(Charsets.UTF_8)
                buf.write(tag12); buf.write(Proto.packVarint(nameBytes.size)); buf.write(nameBytes)
            }
            val result = buf.toByteArray()
            Log.d("SteamCM", "[PROTO] buildProtoHeader: FINAL hex=${result.joinToString("") { "%02X".format(it) }} size=${result.size}")
            return result
        }

        internal fun buildSimpleMsg(v1: Int, v2: Int): ByteArray {
            val buf = ByteArrayOutputStream()
            buf.write(Proto.packVarint(1 shl 3 or 0)); buf.write(Proto.packVarint(v1))
            buf.write(Proto.packVarint(2 shl 3 or 0)); buf.write(Proto.packVarint(v2))
            return buf.toByteArray()
        }
    }

    data class GameInfo(
        val appId: Int,
        val depotIds: List<Int>,
        val displayName: String,
        val estimatedSize: Long
    )

    private val firstRunPrefs = ctx.getSharedPreferences("game_data_downloader", Context.MODE_PRIVATE)

    fun findMissingGames(basedir: File): List<String> {
        val missing = mutableListOf<String>()
        for ((gamedir, _) in GAMES) {
            val dir = File(basedir, gamedir)
            if (!dir.isDirectory || dir.listFiles().isNullOrEmpty()) missing.add(gamedir)
        }
        return if (missing.size == GAMES.size) missing else emptyList()
    }

    fun hasAnyGameData(basedir: File): Boolean {
        return GAMES.keys.any {
            val dir = File(basedir, it)
            dir.isDirectory && dir.listFiles()?.isNotEmpty() == true
        }
    }

    fun hasGamedir(basedir: File, gamedir: String): Boolean {
        val dir = File(basedir, gamedir)
        return dir.isDirectory && dir.listFiles()?.isNotEmpty() == true
    }

    fun estimatedSize(gamedir: String): Long = GAMES[gamedir]?.estimatedSize ?: 0L

    fun hasCheckedFirstRun(): Boolean =
        firstRunPrefs.getBoolean("first_run_checked", false)

    fun setFirstRunChecked() =
        firstRunPrefs.edit().putBoolean("first_run_checked", true).apply()

    suspend fun downloadGame(
        gamedir: String,
        targetDir: File,
        onProgress: suspend (Long, Long) -> Unit,
        onStatus: (String) -> Unit = {},
        depotIds: List<Int>? = null
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val info = GAMES[gamedir] ?: return@withContext Result.failure(IllegalArgumentException("Unknown game: $gamedir"))
            val effectiveIds = if (depotIds != null && depotIds.isNotEmpty()) depotIds else info.depotIds

            val gameDataDir = targetDir
            gameDataDir.mkdirs()

            val client = SteamCMClient { msg ->
                Handler(android.os.Looper.getMainLooper()).post { onStatus(msg) }
            }
            try {
                client.connect()
            } catch (e: Exception) { throw Exception("[connect] ${e.message}", e) }

            val logonResult: Boolean
            try {
                logonResult = client.anonymousLogin()
            } catch (e: Exception) { throw Exception("[login] ${e.message}", e) }
            if (!logonResult) {
                client.disconnect()
                return@withContext Result.failure(Exception("[login] Steam login failed"))
            }

            try {
                val licenseGranted = client.requestLicense(info.appId)
                Log.d("SteamCM", "[download] License for app ${info.appId}: granted=$licenseGranted")
            } catch (e: Exception) { Log.w("SteamCM", "[license] ${e.message}") }

            val cdnHost: String
            try {
                cdnHost = client.requestCDNServerList()
            } catch (e: Exception) { throw Exception("[cdn] ${e.message}", e) }
            Log.d("SteamCM", "[download] CDN host: $cdnHost")

            data class DepotSetup(val depotId: Int, val manifestId: Long, val depotKey: ByteArray, val requestCode: Long)
            val depots = mutableListOf<DepotSetup>()
            for (depotId in effectiveIds) {
                val manifestId = HARDCODED_MANIFEST_IDS[depotId]
                if (manifestId == null || manifestId == 0L) {
                    Log.w("SteamCM", "[download] No hardcoded manifest ID for depot $depotId, skipping")
                    continue
                }
                val depotKey: ByteArray
                try {
                    depotKey = client.getDepotDecryptionKey(info.appId, depotId)
                } catch (e: Exception) { Log.w("SteamCM", "[depotKey] depot $depotId: ${e.message}"); continue }
                val requestCode: Long
                try {
                    requestCode = client.requestManifestRequestCode(depotId, info.appId, manifestId)
                } catch (e: Exception) { Log.w("SteamCM", "[rpc] depot $depotId: ${e.message}"); continue }
                if (requestCode == 0L) {
                    Log.w("SteamCM", "[download] Manifest request code 0 for depot $depotId, skipping")
                    continue
                }
                depots.add(DepotSetup(depotId, manifestId, depotKey, requestCode))
            }
            client.disconnect()

            if (depots.isEmpty()) {
                return@withContext Result.failure(Exception("Could not set up any depots for download"))
            }

            var totalFiles = 0L
            data class DepotFile(val depotSetup: DepotSetup, val file: ManifestFile, val index: Int)
            val allFiles = mutableListOf<DepotFile>()
            for (ds in depots) {
                onStatus("Downloading manifest for depot ${ds.depotId}...")
                val manifestBytes = downloadManifestFromCDN(ds.depotId, ds.manifestId, ds.requestCode, cdnHost)
                if (manifestBytes == null) {
                    Log.w("SteamCM", "[download] Could not download manifest for depot ${ds.depotId}")
                    continue
                }
                val files = parseManifestFiles(manifestBytes, ds.depotKey)
                files.forEachIndexed { i, f -> allFiles.add(DepotFile(ds, f, i)) }
                totalFiles += files.size
                Log.d("SteamCM", "[download] Depot ${ds.depotId}: ${files.size} files from manifest")
            }

            if (allFiles.isEmpty()) {
                return@withContext Result.failure(Exception("No files found in any depot"))
            }

            coroutineScope {
                val semaphore = Semaphore(10)
                val completedFiles = AtomicLong(0L)
                allFiles.map { df ->
                    async {
                        semaphore.withPermit {
                            yield()
                            val relativePath = if (df.file.path.startsWith("$gamedir/"))
                                df.file.path.removePrefix("$gamedir/") else df.file.path
                            val outFile = File(gameDataDir, relativePath)
                            outFile.parentFile?.mkdirs()
                            val success = assembleFileFromChunks(df.depotSetup.depotId, df.file, outFile, df.depotSetup.depotKey, cdnHost)
                            if (!success) {
                                Log.w("SteamCM", "[download] Failed: ${df.file.path}")
                            }
                            val current = completedFiles.incrementAndGet()
                            withContext(Dispatchers.Main) {
                                onProgress(current, totalFiles)
                            }
                        }
                    }
                }.awaitAll()
            }

            Result.success(Unit)
        } catch (e: Exception) {
            e.printStackTrace()
            Result.failure(e)
        }
    }

    data class ChunkInfo(
        val sha1: ByteArray,
        val checksum: Int,
        val offset: Long,
        val compressedLength: Int,
        val uncompressedLength: Int
    )

    data class ManifestFile(
        val path: String,
        val size: Long,
        val crc: Int,
        val chunks: List<ChunkInfo> = emptyList()
    )

    private class ProtoReader(val data: ByteArray) {
        var pos = 0
        val remaining get() = data.size - pos

        fun readVarint(): Int {
            var result = 0
            var shift = 0
            while (pos < data.size) {
                val b = data[pos++].toInt() and 0xFF
                result = result or ((b and 0x7F) shl shift)
                if (b and 0x80 == 0) return result
                shift += 7
            }
            throw Exception("Truncated varint")
        }

        fun readVarint64(): Long {
            var result = 0L
            var shift = 0
            while (pos < data.size) {
                val b = data[pos++].toInt() and 0xFF
                result = result or ((b and 0x7F).toLong() shl shift)
                if (b and 0x80 == 0) return result
                shift += 7
            }
            throw Exception("Truncated varint64")
        }

        fun readBytes(len: Int): ByteArray {
            try {
                val result = data.copyOfRange(pos, pos + len)
                pos += len
                return result
            } catch (e: Exception) {
                throw Exception("Reader.readBytes: pos=$pos len=$len data.size=${data.size}", e)
            }
        }

        fun skipField() {
            if (pos >= data.size) return
            val tag = readVarint()
            val wireType = tag and 0x7
            when (wireType) {
                0 -> readVarint()
                1 -> pos += 8
                2 -> { val len = readVarint(); pos += len }
                3 -> { skipField() }
                4 -> { }
                5 -> pos += 4
                else -> {
                    pos += 1
                }
            }
        }

        fun readLongLE(): Long {
            if (pos + 8 > data.size) throw Exception("Truncated fixed64")
            val result = (data[pos].toLong() and 0xFF) or
                    ((data[pos + 1].toLong() and 0xFF) shl 8) or
                    ((data[pos + 2].toLong() and 0xFF) shl 16) or
                    ((data[pos + 3].toLong() and 0xFF) shl 24) or
                    ((data[pos + 4].toLong() and 0xFF) shl 32) or
                    ((data[pos + 5].toLong() and 0xFF) shl 40) or
                    ((data[pos + 6].toLong() and 0xFF) shl 48) or
                    ((data[pos + 7].toLong() and 0xFF) shl 56)
            pos += 8
            return result
        }

        fun readInt32LE(): Int {
            if (pos + 4 > data.size) throw Exception("Truncated fixed32")
            val result = (data[pos].toInt() and 0xFF) or
                    ((data[pos + 1].toInt() and 0xFF) shl 8) or
                    ((data[pos + 2].toInt() and 0xFF) shl 16) or
                    ((data[pos + 3].toInt() and 0xFF) shl 24)
            pos += 4
            return result
        }

        fun skip(n: Int) { pos += n }
    }

    private inner class SteamCMClient(private val onStatus: (String) -> Unit) {
        private var socket: Socket? = null
        private var output: java.io.OutputStream? = null
        private var input: InputStream? = null
        private var sessionKey = ByteArray(32)
        private var encrypted: Boolean = false
        private var heartbeatJob: Thread? = null
        private var heartbeatSeconds = 9
        private var currentSessionId: Int = 0
        private var currentSteamId: Long = ANON_STEAM_ID

        suspend fun connect() = suspendCancellableCoroutine<Unit> { cont ->
            try {
                for ((host, port) in CM_SERVERS) {
                    try {
                        Log.d("SteamCM", "Connecting to $host:$port...")
                        val sock = Socket()
                        sock.connect(InetSocketAddress(host, port), 10000)
                        sock.soTimeout = 5000
                        socket = sock
                        input = sock.getInputStream()
                        output = sock.getOutputStream()
                        Log.d("SteamCM", "Connected to $host:$port!")
                        onStatus("Connected to Steam")
                        cont.resume(Unit)
                        return@suspendCancellableCoroutine
                    } catch (e: Exception) {
                        Log.d("SteamCM", "Failed $host:$port: ${e.message}")
                    }
                }
                cont.resumeWithException(Exception("No Steam CM server reachable"))
            } catch (e: Exception) {
                cont.resumeWithException(e)
            }
        }

        fun disconnect() {
            try { heartbeatJob?.interrupt() } catch (_: Exception) { }
            try { socket?.close() } catch (_: Exception) { }
        }

        private fun startHeartbeat(intervalSeconds: Int) {
            heartbeatSeconds = intervalSeconds
            heartbeatJob = Thread {
                try {
                    while (!Thread.interrupted()) {
                        Thread.sleep(intervalSeconds * 1000L)
                        if (Thread.interrupted()) break
                        val body = ByteArrayOutputStream()
                        body.write(Proto.packVarint(1 shl 3 or 0))
                        body.write(Proto.packVarint(1))
                        val header = buildProtoHeader(currentSteamId, currentSessionId)
                        sendProtobufMsg(1009, body.toByteArray(), header)
                        Log.d("SteamCM", "[heartbeat] Sent ClientHeartBeat (sendReply=true)")
                    }
                } catch (_: InterruptedException) {
                } catch (e: Exception) {
                    Log.d("SteamCM", "[heartbeat] Error: ${e.message}")
                }
            }.apply { isDaemon = true; start() }
        }

        suspend fun anonymousLogin(): Boolean = suspendCancellableCoroutine { cont ->
            try {
                SecureRandom().nextBytes(sessionKey)
                encrypted = false

                Log.d("SteamCM", "[login] Waiting for ChannelEncryptRequest...")
                val (recvEmsg, encReqBody, isProto) = readMessage()
                Log.d("SteamCM", "[login] Got msg: emsg=$recvEmsg isProto=$isProto bodySize=${encReqBody.size}")
                onStatus("Received encrypt request")
                val encBodyHex = encReqBody.take(16).joinToString("") { "%02X".format(it) } + if (encReqBody.size > 16) "..." else ""
                Log.d("SteamCM", "[login] bodyHex=$encBodyHex")
                if (encReqBody.size < 10 || encReqBody.size > 1000)
                    throw Exception("login: encReqBody SUSPICIOUS: recvEmsg=$recvEmsg size=${encReqBody.size} isProto=$isProto bodyHex=$encBodyHex")
                var off = 0
                val protocolVersion: Int
                val universe: Int
                val keyLen: Int
                val pubKeyData: ByteArray
                val challenge: ByteArray

                if (isProto) {
                    val pr = ProtoReader(encReqBody)
                    pr.skipField()
                    pr.skipField()
                    val field3Tag = pr.readVarint()
                    keyLen = pr.readVarint()
                    if (keyLen > 1000 || keyLen < 0)
                        throw Exception("login proto: SUSPICIOUS keyLen=$keyLen recvEmsg=$recvEmsg encReqBody.size=${encReqBody.size}")
                    pubKeyData = pr.readBytes(keyLen)
                    if (pubKeyData.size < 10)
                        throw Exception("login proto: pubKeyData TOO SMALL: size=${pubKeyData.size} keyLen=$keyLen recvEmsg=$recvEmsg encReqBody.size=${encReqBody.size}")
                    challenge = if (pr.remaining > 0) pr.readBytes(pr.remaining) else ByteArray(0)
                } else {
                    if (encReqBody.size < 8)
                        throw Exception("login binary: body too small: size=${encReqBody.size}")
                    protocolVersion = Proto.readInt32LE(encReqBody, off); off += 4
                    universe = Proto.readInt32LE(encReqBody, off); off += 4
                    keyLen = STEAM_PUBLIC_KEY.size
                    pubKeyData = STEAM_PUBLIC_KEY
                    challenge = if (off < encReqBody.size) encReqBody.copyOfRange(off, encReqBody.size) else ByteArray(0)
                }

                val debugCtx = "isProto=$isProto pubKeyData.size=${pubKeyData.size}"
                val pubKey = if (pubKeyData.contentEquals(STEAM_PUBLIC_KEY)) {
                    KeyFactory.getInstance("RSA").generatePublic(
                        java.security.spec.X509EncodedKeySpec(pubKeyData)
                    )
                } else {
                    val (exp, mod) = parseRSAPubKey(pubKeyData, debugCtx)
                    KeyFactory.getInstance("RSA").generatePublic(
                        RSAPublicKeySpec(BigInteger(1, mod), BigInteger(1, exp))
                    )
                }

                val rsa = Cipher.getInstance("RSA/ECB/OAEPWithSHA-1AndMGF1Padding")
                rsa.init(Cipher.ENCRYPT_MODE, pubKey)
                val blobToEncrypt = sessionKey + challenge
                val encryptedBlob = rsa.doFinal(blobToEncrypt)

                val crc32 = java.util.zip.CRC32()
                crc32.update(encryptedBlob)
                val keyCrc = crc32.value.toInt()

                val encResp = buildEncryptResponse(encryptedBlob, keyCrc)
                Log.d("SteamCM", "[login] Sending encrypt response: size=${encResp.size}")
                sendRaw(encResp)
                onStatus("Sent encrypt response")

                Log.d("SteamCM", "[login] Waiting for ChannelEncryptResult...")
                val (resultEmsg, resultBody, resultIsProto) = readMessage()
                Log.d("SteamCM", "[login] Got result: emsg=$resultEmsg isProto=$resultIsProto bodySize=${resultBody.size}")
                val eresult = Proto.readInt32LE(resultBody, 0)
                Log.d("SteamCM", "[login] eresult=$eresult")
                if (eresult != 1) {
                    throw Exception("Encryption handshake FAILED: eresult=$eresult")
                }
                encrypted = true
                onStatus("Connecting Steam...")

                Log.d("SteamCM", "[login] Sending ClientHello...")
                val helloBody = buildClientHello()
                val helloHeader = buildProtoHeader(ANON_STEAM_ID, 0)
                sendProtobufMsg(9805, helloBody, helloHeader)

                Thread.sleep(100)

                Log.d("SteamCM", "[login] Building ClientLogon message...")
                val logonBody = buildLogonBody()
                val logonHeader = buildProtoHeader(ANON_STEAM_ID, 0)
                sendProtobufMsg(5514, logonBody, logonHeader)
                Log.d("SteamCM", "[login] ClientLogon sent!")

                var success = false
                var tries = 0
                run breaking@{
                    while (tries < 20) {
                        Log.d("SteamCM", "[login] Waiting for logon response... try=$tries")
                        val (emsg, body, isProto) = readMessage()
                        Log.d("SteamCM", "[login] Got response: emsg=$emsg isProto=$isProto bodySize=${body.size}")
                        when (emsg) {
                            1 -> {
                                Log.d("SteamCM", "[login] Got Multi message, parsing sub-messages...")
                                val rdr = ProtoReader(body)
                                var sizeUnzipped = 0u
                                var msgBody: ByteArray? = null
                                while (rdr.remaining > 0) {
                                    val tag = rdr.readVarint()
                                    val fieldNum = tag shr 3
                                    when (fieldNum) {
                                        1 -> { sizeUnzipped = rdr.readVarint().toUInt() }
                                        2 -> { val len = rdr.readVarint(); msgBody = rdr.readBytes(len) }
                                        else -> rdr.skipField()
                                    }
                                }
                                if (msgBody != null) {
                                    var subData = msgBody
                                    if (sizeUnzipped > 0u) {
                                        val gis = GZIPInputStream(ByteArrayInputStream(msgBody))
                                        subData = gis.readBytes()
                                        gis.close()
                                        Log.d("SteamCM", "[login] Multi decompressed: ${msgBody.size} -> ${subData.size}")
                                    }
                                    var off2 = 0
                                    while (off2 < subData.size) {
                                        val subSize = Proto.readInt32LE(subData, off2)
                                        off2 += 4
                                        if (subSize <= 0 || off2 + subSize > subData.size) break
                                        val subMsg = subData.copyOfRange(off2, off2 + subSize)
                                        off2 += subSize
                                        val subEmsg = Proto.readInt32LE(subMsg, 0)
                                        val subIsProto = (subEmsg and PROTO_MASK) != 0
                                        val subEmsgClean = subEmsg and 0x7FFFFFFF
                                        Log.d("SteamCM", "[login] Sub-msg: emsg=$subEmsgClean isProto=$subIsProto size=${subMsg.size}")
                                        if (subEmsgClean == 5501) {
                                            val offHdrLen = Proto.readInt32LE(subMsg, 4)
                                            val offBody = 8 + offHdrLen
                                            val licBody = subMsg.copyOfRange(offBody, subMsg.size)
                                            val licRdr = ProtoReader(licBody)
                                            val packageIds = mutableListOf<Int>()
                                            while (licRdr.remaining > 0) {
                                                val tag = licRdr.readVarint()
                                                val fn = tag shr 3
                                                val wt = tag and 0x7
                                                if (fn == 1 && wt == 0) {
                                                    val eresult = licRdr.readVarint()
                                                    Log.d("SteamCM", "[login] LicenseList eresult=$eresult")
                                                } else if (fn == 2 && wt == 2) {
                                                    val len = licRdr.readVarint()
                                                    val end = licRdr.pos + len
                                                    while (licRdr.pos < end) {
                                                        val lt = licRdr.readVarint()
                                                        val lfn = lt shr 3
                                                        val lwt = lt and 0x7
                                                        if (lfn == 1 && lwt == 0) {
                                                            val pkgId = licRdr.readVarint()
                                                            packageIds.add(pkgId)
                                                        } else {
                                                            when (lwt) {
                                                                0 -> licRdr.readVarint64()
                                                                1 -> licRdr.skip(8)
                                                                2 -> { val ll = licRdr.readVarint(); licRdr.skip(ll) }
                                                                5 -> licRdr.skip(4)
                                                                else -> licRdr.skipField()
                                                            }
                                                        }
                                                    }
                                                } else {
                                                    licRdr.skipField()
                                                }
                                            }
                                            Log.d("SteamCM", "[login] Anonymous user packages: ${packageIds.joinToString(",")}")
                                        } else if (subEmsgClean == 751) {
                                            val headerLen = Proto.readInt32LE(subMsg, 4)
                                            if (headerLen > 0) {
                                                val protoHdr = subMsg.copyOfRange(8, 8 + headerLen)
                                                val hrdr = ProtoReader(protoHdr)
                                                while (hrdr.remaining > 0) {
                                                    val htag = hrdr.readVarint()
                                                    val hfn = htag shr 3
                                                    val hwt = htag and 0x7
                                                    when {
                                                        hfn == 1 && hwt == 1 -> currentSteamId = hrdr.readLongLE()
                                                        hfn == 2 && hwt == 0 -> currentSessionId = hrdr.readVarint()
                                                        else -> hrdr.skipField()
                                                    }
                                                }
                                                Log.d("SteamCM", "[login] SessionId=$currentSessionId SteamId=$currentSteamId")
                                            }
                                            val subOff = 8 + headerLen
                                            val subBody = subMsg.copyOfRange(subOff, subMsg.size)
                                            val subRdr = ProtoReader(subBody)
                                            val eresultTag = subRdr.readVarint()
                                            val loginResult = subRdr.readVarint()
                                            Log.d("SteamCM", "[login] Logon response eresult=$loginResult (tag=$eresultTag) headerLen=$headerLen subOff=$subOff subBody.size=${subBody.size} subBodyHex=${subBody.take(16).joinToString("") { "%02X".format(it) }}")
                                            if (loginResult == 1) {
                                                Log.d("SteamCM", "[login] Logon success!")
                                                success = true
                                                while (subRdr.remaining > 0) {
                                                    val tag = subRdr.readVarint()
                                                    val fn = tag shr 3
                                                    val wt = tag and 0x7
                                                    if (fn == 2 && wt == 0) {
                                                        heartbeatSeconds = subRdr.readVarint()
                                                        Log.d("SteamCM", "[login] heartbeat_seconds=$heartbeatSeconds")
                                                    } else {
                                                        when (wt) {
                                                            0 -> subRdr.readVarint64()
                                                            1 -> subRdr.skip(8)
                                                            2 -> { val len = subRdr.readVarint(); subRdr.skip(len) }
                                                            5 -> subRdr.skip(4)
                                                            else -> { if (subRdr.remaining > 0) subRdr.readVarint64() }
                                                        }
                                                    }
                                                }
                                                startHeartbeat(heartbeatSeconds)
                                            } else {
                                                Log.e("SteamCM", "[login] Logon FAILED: eresult=$loginResult")
                                            }
                                            return@breaking
                                        }
                                    }
                                }
                            }
                            751 -> {
                                val rdr = ProtoReader(body)
                                val eresultTag = rdr.readVarint()
                                val loginResult = rdr.readVarint()
                                Log.d("SteamCM", "[login] Direct logon response eresult=$loginResult (tag=$eresultTag)")
                                if (loginResult == 1) {
                                    Log.d("SteamCM", "[login] Logon success!")
                                    success = true
                                } else {
                                    Log.e("SteamCM", "[login] Logon FAILED: eresult=$loginResult")
                                }
                                return@breaking
                            }
                            753 -> { Log.d("SteamCM", "[login] Logon failure"); return@breaking }
                            754, 756, 5514 -> { Log.d("SteamCM", "[login] Ignoring emsg=$emsg") }
                            else -> { Log.d("SteamCM", "[login] Unexpected emsg=$emsg") }
                        }
                        tries++
                    }
                }
                if (!success) {
                    Log.w("SteamCM", "[login] Logon failed after $tries attempts")
                }

                cont.resume(success)
            } catch (e: Exception) {
                cont.resumeWithException(e)
            }
        }

        suspend fun requestLicense(appId: Int): Boolean = suspendCancellableCoroutine { cont ->
            try {
                val body = ByteArrayOutputStream()
                body.write(Proto.packVarint(2 shl 3 or 0))
                body.write(Proto.packVarint(appId))
                val header = buildProtoHeader(currentSteamId, currentSessionId)
                sendProtobufMsg(5572, body.toByteArray(), header)
                Log.d("SteamCM", "[license] Requested free license for app $appId")

                var gotLicense = false
                var tries = 0
                while (tries < 15) {
                    val (emsg, data, isProto) = readMessage()
                    Log.d("SteamCM", "[license] Got emsg=$emsg isProto=$isProto size=${data.size}")
                    if (emsg == 1) {
                        val rdr = ProtoReader(data)
                        var sizeUnzipped = 0u
                        var msgBody: ByteArray? = null
                        while (rdr.remaining > 0) {
                            val tag = rdr.readVarint()
                            val fn = tag shr 3
                            when (fn) {
                                1 -> { sizeUnzipped = rdr.readVarint().toUInt() }
                                2 -> { val len = rdr.readVarint(); msgBody = rdr.readBytes(len) }
                                else -> rdr.skipField()
                            }
                        }
                        if (msgBody != null) {
                            var subData = msgBody
                            if (sizeUnzipped > 0u) {
                                val gis = GZIPInputStream(ByteArrayInputStream(msgBody))
                                subData = gis.readBytes()
                                gis.close()
                                Log.d("SteamCM", "[license] Multi decompressed: ${msgBody.size} -> ${subData.size}")
                            }
                            var off2 = 0
                            while (off2 < subData.size) {
                                val subSize = Proto.readInt32LE(subData, off2)
                                off2 += 4
                                if (subSize <= 0 || off2 + subSize > subData.size) break
                                val subMsg = subData.copyOfRange(off2, off2 + subSize)
                                off2 += subSize
                                val subEmsg = Proto.readInt32LE(subMsg, 0) and 0x7FFFFFFF
                                Log.d("SteamCM", "[license] Sub-msg emsg=$subEmsg size=${subMsg.size}")
                                if (subEmsg == 5573) {
                                    val subHdrLen = Proto.readInt32LE(subMsg, 4)
                                    val subOff = 8 + subHdrLen
                                    val subBody = subMsg.copyOfRange(subOff, subMsg.size)
                                    val subRdr = ProtoReader(subBody)
                                    while (subRdr.remaining > 0) {
                                        val tag = subRdr.readVarint()
                                        val fn = tag shr 3
                                        val wt = tag and 0x7
                                        if (fn == 3 && wt == 0) {
                                            val grantedAppId = subRdr.readVarint()
                                            if (grantedAppId == appId) {
                                                gotLicense = true
                                                Log.d("SteamCM", "[license] Granted app $appId!")
                                            }
                                        } else {
                                            subRdr.skipField()
                                        }
                                    }
                                    if (gotLicense) break
                                }
                            }
                        }
                        if (gotLicense) break
                    } else if (emsg == 5573) {
                        val rdr = ProtoReader(data)
                        while (rdr.remaining > 0) {
                            val tag = rdr.readVarint()
                            val fn = tag shr 3
                            val wt = tag and 0x7
                            if (fn == 3 && wt == 0) {
                                val grantedAppId = rdr.readVarint()
                                if (grantedAppId == appId) {
                                    gotLicense = true
                                    Log.d("SteamCM", "[license] Granted app $appId!")
                                }
                            } else {
                                rdr.skipField()
                            }
                        }
                        break
                    } else if (emsg == 757) {
                        Log.e("SteamCM", "[license] Got logged off")
                        break
                    } else if (emsg == 751) {
                        Log.d("SteamCM", "[license] Ignoring logon response")
                    } else {
                        Log.d("SteamCM", "[license] Ignoring emsg=$emsg")
                    }
                    tries++
                }
                Log.d("SteamCM", "[license] Result: gotLicense=$gotLicense after $tries attempts")
                cont.resume(gotLicense)
            } catch (e: Exception) {
                Log.e("SteamCM", "[license] Exception: ${e.message}")
                cont.resumeWithException(e)
            }
        }

        suspend fun getDepotDecryptionKey(appId: Int, depotId: Int): ByteArray =
            suspendCancellableCoroutine { cont ->
                try {
                    val body = buildSimpleMsg(depotId, appId)
                    val header = buildProtoHeader(currentSteamId, currentSessionId)
                    sendProtobufMsg(5438, body, header)
                    Log.d("SteamCM", "[depotKey] Requested key for depot $depotId app $appId")

                    var gotKey = false
                    var tries = 0
                    while (tries < 15 && !gotKey) {
                        val (emsg, data, isProto) = readMessage()
                        Log.d("SteamCM", "[depotKey] Got emsg=$emsg isProto=$isProto size=${data.size}")
                        if (emsg == 1) {
                            val rdr = ProtoReader(data)
                            var sizeUnzipped = 0u
                            var msgBody: ByteArray? = null
                            while (rdr.remaining > 0) {
                                val tag = rdr.readVarint()
                                val fn = tag shr 3
                                when (fn) {
                                    1 -> { sizeUnzipped = rdr.readVarint().toUInt() }
                                    2 -> { val len = rdr.readVarint(); msgBody = rdr.readBytes(len) }
                                    else -> rdr.skipField()
                                }
                            }
                            if (msgBody != null) {
                                var subData = msgBody
                                if (sizeUnzipped > 0u) {
                                    val gis = GZIPInputStream(ByteArrayInputStream(msgBody))
                                    subData = gis.readBytes()
                                    gis.close()
                                    Log.d("SteamCM", "[depotKey] Multi decompressed: ${msgBody.size} -> ${subData.size}")
                                }
                                var off2 = 0
                                while (off2 < subData.size) {
                                    val subSize = Proto.readInt32LE(subData, off2)
                                    off2 += 4
                                    if (subSize <= 0 || off2 + subSize > subData.size) break
                                    val subMsg = subData.copyOfRange(off2, off2 + subSize)
                                    off2 += subSize
                                    val subEmsg = Proto.readInt32LE(subMsg, 0) and 0x7FFFFFFF
                                    Log.d("SteamCM", "[depotKey] Sub-msg emsg=$subEmsg size=${subMsg.size}")
                                    if (subEmsg == 5439) {
                                        val subHdrLen = Proto.readInt32LE(subMsg, 4)
                                        val subOff = 8 + subHdrLen
                                        val subBody = subMsg.copyOfRange(subOff, subMsg.size)
                                        val subRdr = ProtoReader(subBody)
                                        subRdr.skipField()
                                        subRdr.skipField()
                                        subRdr.readVarint()
                                        val klen = subRdr.readVarint()
                                        val key = subRdr.readBytes(klen)
                                        Log.d("SteamCM", "[depotKey] Got key size=${key.size}")
                                        cont.resume(key)
                                        gotKey = true
                                        break
                                    }
                                }
                            }
                        } else if (emsg == 5439) {
                            val rdr = ProtoReader(data)
                            rdr.skipField()
                            rdr.skipField()
                            rdr.readVarint()
                            val klen = rdr.readVarint()
                            val key = rdr.readBytes(klen)
                            Log.d("SteamCM", "[depotKey] Got key size=${key.size}")
                            cont.resume(key)
                            gotKey = true
                        } else if (emsg == 757) {
                            cont.resumeWithException(Exception("Got logged off while requesting depot key"))
                            gotKey = true
                        } else {
                            Log.d("SteamCM", "[depotKey] Ignoring emsg=$emsg")
                        }
                        tries++
                    }
                    if (!gotKey) {
                        cont.resumeWithException(Exception("Expected key response, timed out after $tries attempts"))
                    }
                } catch (e: Exception) {
                    cont.resumeWithException(e)
                }
            }

        suspend fun requestCDNServerList(): String = suspendCancellableCoroutine { cont ->
            try {
                val body = ByteArrayOutputStream()
                body.write(Proto.packVarint(1 shl 3 or 0)); body.write(Proto.packVarint(1))
                body.write(Proto.packVarint(2 shl 3 or 0)); body.write(Proto.packVarint(5))
                val header = buildProtoHeader(currentSteamId, currentSessionId, "ContentServerDirectory.GetServersForSteamPipe#1", nextJobId.getAndIncrement())
                sendProtobufMsg(151, body.toByteArray(), header)
                Log.d("SteamCM", "[cdn] Requested server list")

                var tries = 0
                while (tries < 15) {
                    val (emsg, data, isProto) = readMessage()
                    Log.d("SteamCM", "[cdn] Got emsg=$emsg isProto=$isProto size=${data.size}")
                    if (emsg == 1) {
                        val rdr = ProtoReader(data)
                        var sizeUnzipped = 0u
                        var msgBody: ByteArray? = null
                        while (rdr.remaining > 0) {
                            val tag = rdr.readVarint()
                            val fn = tag shr 3
                            when (fn) {
                                1 -> { sizeUnzipped = rdr.readVarint().toUInt() }
                                2 -> { val len = rdr.readVarint(); msgBody = rdr.readBytes(len) }
                                else -> rdr.skipField()
                            }
                        }
                        if (msgBody != null) {
                            var subData = msgBody
                            if (sizeUnzipped > 0u) {
                                val gis = GZIPInputStream(ByteArrayInputStream(msgBody))
                                subData = gis.readBytes()
                                gis.close()
                                Log.d("SteamCM", "[cdn] Multi decompressed: ${msgBody.size} -> ${subData.size}")
                            }
                            var off2 = 0
                            while (off2 < subData.size) {
                                val subSize = Proto.readInt32LE(subData, off2)
                                off2 += 4
                                if (subSize <= 0 || off2 + subSize > subData.size) break
                                val subMsg = subData.copyOfRange(off2, off2 + subSize)
                                off2 += subSize
                                val subEmsg = Proto.readInt32LE(subMsg, 0) and 0x7FFFFFFF
                                if (subEmsg == 147) {
                                    val subHdrLen = Proto.readInt32LE(subMsg, 4)
                                    val subOff = 8 + subHdrLen
                                    val subBody = subMsg.copyOfRange(subOff, subMsg.size)
                                    val subRdr = ProtoReader(subBody)
                                    while (subRdr.remaining > 0) {
                                        val stag = subRdr.readVarint()
                                        val sfn = stag shr 3
                                        val swt = stag and 0x7
                                        if (sfn == 1 && swt == 2) {
                                            val serverLen = subRdr.readVarint()
                                            val serverBytes = subRdr.readBytes(serverLen)
                                            val serverRdr = ProtoReader(serverBytes)
                                            var host: String? = null
                                            var vhost: String? = null
                                            var httpsSupport: String? = null
                                            while (serverRdr.remaining > 0) {
                                                val ftag = serverRdr.readVarint()
                                                val ffn = ftag shr 3
                                                val fwt = ftag and 0x7
                                                if (ffn == 8 && fwt == 2) {
                                                    val hLen = serverRdr.readVarint()
                                                    host = String(serverRdr.readBytes(hLen), Charsets.UTF_8)
                                                } else if (ffn == 9 && fwt == 2) {
                                                    val hLen = serverRdr.readVarint()
                                                    vhost = String(serverRdr.readBytes(hLen), Charsets.UTF_8)
                                                } else if (ffn == 12 && fwt == 2) {
                                                    val hLen = serverRdr.readVarint()
                                                    httpsSupport = String(serverRdr.readBytes(hLen), Charsets.UTF_8)
                                                } else {
                                                    when (fwt) { 0 -> serverRdr.readVarint64(); 1 -> serverRdr.skip(8); 2 -> { val l = serverRdr.readVarint(); serverRdr.skip(l) }; 5 -> serverRdr.skip(4); else -> serverRdr.skipField() }
                                                }
                                            }
                                            if (host != null && !host.isNullOrEmpty()) {
                                                val useHost = if (vhost != null && vhost.isNotEmpty()) vhost else host
                                                Log.d("SteamCM", "[cdn] Using CDN server: host=$host vhost=$vhost https=$httpsSupport")
                                                cont.resume(useHost)
                                                return@suspendCancellableCoroutine
                                            }
                                        } else {
                                            when (swt) { 0 -> subRdr.readVarint64(); 1 -> subRdr.skip(8); 2 -> { val l = subRdr.readVarint(); subRdr.skip(l) }; 5 -> subRdr.skip(4); else -> subRdr.skipField() }
                                        }
                                    }
                                }
                            }
                        }
                    } else if (emsg == 147) {
                        val rdr = ProtoReader(data)
                        while (rdr.remaining > 0) {
                            val tag = rdr.readVarint()
                            val fn = tag shr 3
                            val wt = tag and 0x7
                            if (fn == 1 && wt == 2) {
                                val serverLen = rdr.readVarint()
                                val serverBytes = rdr.readBytes(serverLen)
                                val serverRdr = ProtoReader(serverBytes)
                                var host: String? = null
                                var vhost: String? = null
                                while (serverRdr.remaining > 0) {
                                    val ftag = serverRdr.readVarint()
                                    val ffn = ftag shr 3
                                    val fwt = ftag and 0x7
                                    if (ffn == 8 && fwt == 2) {
                                        val hLen = serverRdr.readVarint()
                                        host = String(serverRdr.readBytes(hLen), Charsets.UTF_8)
                                    } else if (ffn == 9 && fwt == 2) {
                                        val hLen = serverRdr.readVarint()
                                        vhost = String(serverRdr.readBytes(hLen), Charsets.UTF_8)
                                    } else {
                                        when (fwt) { 0 -> serverRdr.readVarint64(); 1 -> serverRdr.skip(8); 2 -> { val l = serverRdr.readVarint(); serverRdr.skip(l) }; 5 -> serverRdr.skip(4); else -> serverRdr.skipField() }
                                    }
                                }
                                if (host != null && !host.isNullOrEmpty()) {
                                    val useHost = if (vhost != null && vhost.isNotEmpty()) vhost else host
                                    Log.d("SteamCM", "[cdn] Using CDN server (direct): $useHost")
                                    cont.resume(useHost)
                                    return@suspendCancellableCoroutine
                                }
                            } else {
                                when (wt) { 0 -> rdr.readVarint64(); 1 -> rdr.skip(8); 2 -> { val l = rdr.readVarint(); rdr.skip(l) }; 5 -> rdr.skip(4); else -> rdr.skipField() }
                            }
                        }
                    } else if (emsg == 757) {
                        Log.e("SteamCM", "[cdn] Got logged off")
                        break
                    }
                    tries++
                }
                cont.resumeWithException(Exception("Could not get CDN server list"))
            } catch (e: Exception) {
                Log.e("SteamCM", "[cdn] Exception: ${e.message}")
                cont.resumeWithException(e)
            }
        }

        suspend fun requestManifestRequestCode(depotId: Int, appId: Int, manifestId: Long): Long = suspendCancellableCoroutine { cont ->
            try {
                val body = ByteArrayOutputStream()
                body.write(Proto.packVarint(1 shl 3 or 0)); body.write(Proto.packVarint(appId))
                body.write(Proto.packVarint(2 shl 3 or 0)); body.write(Proto.packVarint(depotId))
                body.write(Proto.packVarint(3 shl 3 or 0)); body.write(Proto.packVarint64(manifestId))
                val header = buildProtoHeader(currentSteamId, currentSessionId, "ContentServerDirectory.GetManifestRequestCode#1", nextJobId.getAndIncrement())
                sendProtobufMsg(151, body.toByteArray(), header)
                Log.d("SteamCM", "[rpc] Requested manifest request code for depot $depotId app $appId manifest $manifestId")

                var tries = 0
                while (tries < 15) {
                    val (emsg, data, isProto) = readMessage()
                    Log.d("SteamCM", "[rpc] Got emsg=$emsg isProto=$isProto size=${data.size}")
                    if (emsg == 1) {
                        val rdr = ProtoReader(data)
                        var sizeUnzipped = 0u
                        var msgBody: ByteArray? = null
                        while (rdr.remaining > 0) {
                            val tag = rdr.readVarint()
                            val fn = tag shr 3
                            when (fn) {
                                1 -> { sizeUnzipped = rdr.readVarint().toUInt() }
                                2 -> { val len = rdr.readVarint(); msgBody = rdr.readBytes(len) }
                                else -> rdr.skipField()
                            }
                        }
                        if (msgBody != null) {
                            var subData = msgBody
                            if (sizeUnzipped > 0u) {
                                val gis = GZIPInputStream(ByteArrayInputStream(msgBody))
                                subData = gis.readBytes()
                                gis.close()
                                Log.d("SteamCM", "[rpc] Multi decompressed: ${msgBody.size} -> ${subData.size}")
                            }
                            var off2 = 0
                            while (off2 < subData.size) {
                                val subSize = Proto.readInt32LE(subData, off2)
                                off2 += 4
                                if (subSize <= 0 || off2 + subSize > subData.size) break
                                val subMsg = subData.copyOfRange(off2, off2 + subSize)
                                off2 += subSize
                                val subEmsg = Proto.readInt32LE(subMsg, 0) and 0x7FFFFFFF
                                Log.d("SteamCM", "[rpc] Sub-msg emsg=$subEmsg size=${subMsg.size}")
                                if (subEmsg == 147) {
                                    val subHdrLen = Proto.readInt32LE(subMsg, 4)
                                    val subOff = 8 + subHdrLen
                                    val subBody = subMsg.copyOfRange(subOff, subMsg.size)
                                    val subRdr = ProtoReader(subBody)
                                    var code = 0L
                                    while (subRdr.remaining > 0) {
                                        val stag = subRdr.readVarint()
                                        val sfn = stag shr 3
                                        val swt = stag and 0x7
                                        if (sfn == 1 && swt == 0) {
                                            code = subRdr.readVarint64()
                                        } else {
                                            when (swt) { 0 -> subRdr.readVarint64(); 1 -> subRdr.skip(8); 2 -> { val l = subRdr.readVarint(); subRdr.skip(l) }; 5 -> subRdr.skip(4); else -> subRdr.skipField() }
                                        }
                                    }
                                    Log.d("SteamCM", "[rpc] Got manifest request code=$code (from Multi)")
                                    cont.resume(code)
                                    return@suspendCancellableCoroutine
                                }
                            }
                        }
                    } else if (emsg == 147) {
                        val rdr = ProtoReader(data)
                        var code = 0L
                        while (rdr.remaining > 0) {
                            val tag = rdr.readVarint()
                            val fn = tag shr 3
                            val wt = tag and 0x7
                            if (fn == 1 && wt == 0) {
                                code = rdr.readVarint64()
                            } else {
                                when (wt) { 0 -> rdr.readVarint64(); 1 -> rdr.skip(8); 2 -> { val l = rdr.readVarint(); rdr.skip(l) }; 5 -> rdr.skip(4); else -> rdr.skipField() }
                            }
                        }
                        Log.d("SteamCM", "[rpc] Got manifest request code=$code")
                        cont.resume(code)
                        return@suspendCancellableCoroutine
                    } else if (emsg == 757) {
                        Log.e("SteamCM", "[rpc] Got logged off")
                        cont.resume(0L)
                        return@suspendCancellableCoroutine
                    } else {
                        Log.d("SteamCM", "[rpc] Ignoring emsg=$emsg")
                    }
                    tries++
                }
                cont.resume(0L)
            } catch (e: Exception) {
                Log.e("SteamCM", "[rpc] Exception: ${e.message}")
                cont.resume(0L)
            }
        }

        private fun readMessage(): Triple<Int, ByteArray, Boolean> {
            val packetLen: Int
            val rawFirst4: String
            try {
                val lenBytes = ByteArray(4)
                var off = 0
                while (off < 4) {
                    val r = input!!.read(lenBytes, off, 4 - off)
                    if (r == -1) throw Exception("Connection closed while reading packetLen")
                    off += r
                }
                packetLen = (lenBytes[0].toInt() and 0xFF) or
                        ((lenBytes[1].toInt() and 0xFF) shl 8) or
                        ((lenBytes[2].toInt() and 0xFF) shl 16) or
                        ((lenBytes[3].toInt() and 0xFF) shl 24)
                rawFirst4 = lenBytes.joinToString("") { "%02X".format(it) }
                Log.d("SteamCM", "[read] packetLen=$packetLen lenBytes=$rawFirst4")
                
                val magicBytes = ByteArray(4)
                off = 0
                while (off < 4) {
                    val r = input!!.read(magicBytes, off, 4 - off)
                    if (r == -1) throw Exception("Connection closed while reading magic")
                    off += r
                }
                val magic = (magicBytes[0].toInt() and 0xFF) or
                        ((magicBytes[1].toInt() and 0xFF) shl 8) or
                        ((magicBytes[2].toInt() and 0xFF) shl 16) or
                        ((magicBytes[3].toInt() and 0xFF) shl 24)
                Log.d("SteamCM", "[read] magic=0x%08X".format(magic))
                if (magic != TCP_MAGIC)
                    throw Exception("readMessage: INVALID MAGIC! expected=0x31305456 got=0x%08X".format(magic))
                
                if (packetLen < 4 || packetLen > 1000000)
                    throw Exception("readMessage: SUSPICIOUS packetLen=$packetLen hex=$rawFirst4 encrypted=$encrypted")
            } catch (e: Exception) {
                Log.e("SteamCM", "[read] ERROR: ${e.message}")
                throw Exception("readMessage: read packetLen failed: ${e.message}", e)
            }
            val encryptedPayload: ByteArray
            try {
                val remainingLen = packetLen
                encryptedPayload = ByteArray(remainingLen)
                var off = 0
                while (off < remainingLen) {
                    val r = input!!.read(encryptedPayload, off, remainingLen - off)
                    if (r == -1) throw Exception("Connection closed at $off/$remainingLen")
                    off += r
                }
            } catch (e: Exception) {
                throw Exception("readMessage: readExact packetLen=$packetLen size=$packetLen failed", e)
            }
            val payloadHex = encryptedPayload.take(16).joinToString("") { "%02X".format(it) } + if (encryptedPayload.size > 16) "..." else ""
            val payload = if (encrypted) {
                Log.d("SteamCM", "[read] encrypted=true, trying to decrypt ${encryptedPayload.size} bytes...")
                try {
                    val decrypted = decryptAES(encryptedPayload)
                    Log.d("SteamCM", "[read] decrypted=${decrypted.size} bytes hex=${decrypted.take(32).joinToString("") { "%02X".format(it) }}...")
                    if (decrypted.size < 8)
                        throw Exception("readMessage: decrypt result TOO SMALL: ${decrypted.size} bytes hex=${decrypted.take(16).joinToString("") { "%02X".format(it) }}")
                    decrypted
                } catch (e: Exception) {
                    Log.e("SteamCM", "[read] decrypt FAILED: ${e.message}")
                    encryptedPayload
                }
            } else {
                encryptedPayload
            }
            if (payload.size < 8)
                throw Exception("readMessage: SUSPICIOUS payload.size=${payload.size} encrypted=$encrypted")

            var off = 0
            val rawEmsg = readInt32LE(payload, off); off += 4
            val isProto = (rawEmsg and PROTO_MASK) != 0
            val emsg = rawEmsg and 0x7FFFFFFF
            val rawPayloadHex = payload.take(48).joinToString("") { "%02X".format(it) } + if (payload.size > 48) " [${payload.size} bytes total]" else ""
            val payloadHexForError = payload.take(32).joinToString("") { "%02X".format(it) } + if (payload.size > 32) "..." else ""

            val body: ByteArray
            if (isProto) {
                val headerLen: Int
                try {
                    headerLen = readInt32LE(payload, off); off += 4
                } catch (e: Exception) {
                    throw Exception("readMessage: read headerLen at off=$off payload.size=${payload.size} emsg=$emsg rawEmsg=$rawEmsg payloadHex=$payloadHexForError", e)
                }
                if (headerLen > payload.size - off)
                    throw Exception("readMessage: headerLen=$headerLen exceeds remaining=${payload.size - off} emsg=$emsg payload.size=${payload.size} payloadHex=$payloadHexForError")
                if (headerLen > 0) off += headerLen
                try {
                    body = payload.copyOfRange(off, payload.size)
                } catch (e: Exception) {
                    throw Exception("readMessage: proto copyOfRange off=$off payload.size=${payload.size} headerLen=$headerLen emsg=$emsg isProto=$isProto", e)
                }
            } else {
                if (payload.size < 20)
                    throw Exception("readMessage LEGACY: payload TOO SMALL: size=${payload.size} emsg=$emsg rawEmsg=$rawEmsg rawPayloadHex=$rawPayloadHex")
                off += 16
                try {
                    body = payload.copyOfRange(off, payload.size)
                } catch (e: Exception) {
                    throw Exception("readMessage: legacy copyOfRange off=$off payload.size=${payload.size} emsg=$emsg isProto=$isProto rawPayloadHex=$rawPayloadHex", e)
                }
            }
            val bodyHex = body.take(16).joinToString("") { "%02X".format(it) } + if (body.size > 16) "..." else ""

            return Triple(emsg, body, isProto)
        }

        private fun sendRaw(data: ByteArray) {
            val final = ByteArray(data.size + 8)
            Proto.packInt32(data.size).copyInto(final, 0)
            Proto.packInt32(TCP_MAGIC).copyInto(final, 4)
            data.copyInto(final, 8)
            Log.d("SteamCM", "[sendRaw] sending ${final.size} bytes: firstBytes=${final.take(24).joinToString("") { "%02X".format(it) }}...")
            output!!.write(final)
            output!!.flush()
            Log.d("SteamCM", "[sendRaw] sent!")
        }

        private fun sendProtobufMsg(emsg: Int, body: ByteArray, header: ByteArray?) {
            Log.d("SteamCM", "[sendProtobuf] emsg=$emsg encrypted=$encrypted bodySize=${body.size}")
            val bos = ByteArrayOutputStream()
            val emsgWithProto = emsg or PROTO_MASK
            bos.write(Proto.packInt32(emsgWithProto))
            val headerBytes = if (header != null) header else ByteArray(0)
            bos.write(Proto.packInt32(headerBytes.size))
            bos.write(headerBytes)
            bos.write(body)

            val rawPayload = bos.toByteArray()
            Log.d("SteamCM", "[sendProtobuf] rawPayload FULL hex=${rawPayload.joinToString("") { "%02X".format(it) }}")
            val finalPayload = if (encrypted) encryptAES(rawPayload) else rawPayload
            Log.d("SteamCM", "[sendProtobuf] finalPayload hex=${finalPayload.take(48).joinToString("") { "%02X".format(it) }}...")

            val final = ByteArray(finalPayload.size + 8)
            Proto.packInt32(finalPayload.size).copyInto(final, 0)
            Proto.packInt32(TCP_MAGIC).copyInto(final, 4)
            finalPayload.copyInto(final, 8)
            Log.d("SteamCM", "[sendProtobuf] TCP packet FULL hex=${final.joinToString("") { "%02X".format(it) }}")
            output!!.write(final)
            output!!.flush()
        }

        private fun encryptAES(data: ByteArray): ByteArray {
            val random3 = ByteArray(3)
            SecureRandom().nextBytes(random3)

            val hmac = Mac.getInstance("HmacSHA1")
            hmac.init(SecretKeySpec(sessionKey.copyOfRange(0, 16), "HmacSHA1"))
            hmac.update(random3)
            hmac.update(data)
            val hash = hmac.doFinal()

            val iv = hash.copyOfRange(0, 13) + random3

            val ecb = Cipher.getInstance("AES/ECB/NoPadding")
            ecb.init(Cipher.ENCRYPT_MODE, SecretKeySpec(sessionKey, "AES"))
            val ecbIv = ecb.doFinal(iv)

            val cbc = Cipher.getInstance("AES/CBC/PKCS5Padding")
            cbc.init(Cipher.ENCRYPT_MODE, SecretKeySpec(sessionKey, "AES"), IvParameterSpec(iv))
            val ciphertext = cbc.doFinal(data)

            val result = ecbIv + ciphertext
            Log.d("SteamCM", "[encryptAES] input=${data.size} output=${result.size} random3=${random3.joinToString("") { "%02X".format(it) }} iv=${iv.take(16).joinToString("") { "%02X".format(it) }}")
            return result
        }

        private fun decryptAES(data: ByteArray): ByteArray {
            Log.d("SteamCM", "[decryptAES] input=${data.size} hex=${data.take(16).joinToString("") { "%02X".format(it) }}...")
            val ecb = Cipher.getInstance("AES/ECB/NoPadding")
            ecb.init(Cipher.DECRYPT_MODE, SecretKeySpec(sessionKey, "AES"))
            val iv: ByteArray
            try {
                iv = ecb.doFinal(data.copyOfRange(0, 16))
                Log.d("SteamCM", "[decryptAES] iv=${iv.joinToString("") { "%02X".format(it) }}")
            } catch (e: Exception) {
                throw Exception("decryptAES: ECB data.size=${data.size} copyOfRange(0,16)", e)
            }

            val plaintext: ByteArray
            try {
                val body = data.copyOfRange(16, data.size)
                val cbc = Cipher.getInstance("AES/CBC/PKCS5Padding")
                cbc.init(Cipher.DECRYPT_MODE, SecretKeySpec(sessionKey, "AES"), IvParameterSpec(iv))
                plaintext = cbc.doFinal(body)
            } catch (e: Exception) {
                throw Exception("decryptAES: CBC data.size=${data.size} iv.size=${iv.size} copyOfRange(16,${data.size})", e)
            }

            try {
                val hmac = Mac.getInstance("HmacSHA1")
                hmac.init(SecretKeySpec(sessionKey.copyOfRange(0, 16), "HmacSHA1"))
                hmac.update(iv.copyOfRange(13, 16))
                hmac.update(plaintext)
                val hash = hmac.doFinal()

                if (!hash.copyOfRange(0, 13).contentEquals(iv.copyOfRange(0, 13))) {
                    throw Exception("HMAC verification failed")
                }
            } catch (e: Exception) {
                if (e.message == "HMAC verification failed") throw e
                throw Exception("decryptAES: HMAC verification data.size=${data.size} iv.size=${iv.size} plaintext.size=${plaintext.size}", e)
            }

            return plaintext
        }

        private fun readInt32LE(data: ByteArray, offset: Int): Int {
            return (data[offset].toInt() and 0xFF) or
                    ((data[offset + 1].toInt() and 0xFF) shl 8) or
                    ((data[offset + 2].toInt() and 0xFF) shl 16) or
                    ((data[offset + 3].toInt() and 0xFF) shl 24)
        }
    }

    private class ByteArrayOutputStream {
        private val data = mutableListOf<Byte>()
        fun write(b: ByteArray) { data.addAll(b.toList()) }
        fun write(b: Byte) { data.add(b) }
        fun toByteArray(): ByteArray = data.toByteArray()
    }

    private object Proto {
        fun packVarint(value: Int): ByteArray {
            val bos = ByteArrayOutputStream()
            var v = value
            while (v and -0x80 != 0) {
                bos.write(((v and 0x7F) or 0x80).toByte())
                v = v ushr 7
            }
            bos.write((v and 0x7F).toByte())
            return bos.toByteArray()
        }

        fun packVarint64(value: Long): ByteArray {
            val bos = ByteArrayOutputStream()
            var v = value
            while (v and -0x80L != 0L) {
                bos.write(((v and 0x7F) or 0x80).toByte())
                v = v ushr 7
            }
            bos.write((v and 0x7F).toByte())
            return bos.toByteArray()
        }

        fun packInt32(v: Int) = byteArrayOf(
            (v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(),
            ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte()
        )

        fun packInt64(v: Long): ByteArray = byteArrayOf(
            (v and 0xFF).toByte(),
            ((v shr 8) and 0xFF).toByte(),
            ((v shr 16) and 0xFF).toByte(),
            ((v shr 24) and 0xFF).toByte(),
            ((v shr 32) and 0xFF).toByte(),
            ((v shr 40) and 0xFF).toByte(),
            ((v shr 48) and 0xFF).toByte(),
            ((v shr 56) and 0xFF).toByte()
        )

        fun encodeZigZag(v: Int) = (v ushr 1) xor -(v and 1)

        fun decodeZigZag(v: Int) = (v ushr 1) xor -(v and 1)

        fun readInt32LE(input: InputStream): Int {
            val b = ByteArray(4)
            var off = 0
            while (off < 4) {
                val r = input.read(b, off, 4 - off)
                if (r == -1) throw Exception("Connection closed")
                off += r
            }
            return (b[0].toInt() and 0xFF) or ((b[1].toInt() and 0xFF) shl 8) or
                    ((b[2].toInt() and 0xFF) shl 16) or ((b[3].toInt() and 0xFF) shl 24)
        }

        fun readInt32LE(data: ByteArray, offset: Int): Int {
            return (data[offset].toInt() and 0xFF) or
                    ((data[offset + 1].toInt() and 0xFF) shl 8) or
                    ((data[offset + 2].toInt() and 0xFF) shl 16) or
                    ((data[offset + 3].toInt() and 0xFF) shl 24)
        }

        fun readExact(input: InputStream, len: Int): ByteArray {
            val data = ByteArray(len)
            var off = 0
            while (off < len) {
                val r = input.read(data, off, len - off)
                if (r == -1) throw Exception("Connection closed")
                off += r
            }
            return data
        }
    }

    data class KVNode(val name: String, val value: String = "", val children: MutableList<KVNode> = mutableListOf()) {
        fun child(name: String): KVNode? = children.find { it.name == name }
        fun childValue(name: String): String? = child(name)?.value
    }

    fun parseKeyValueText(text: String): KVNode? {
        var pos = 0
        fun skipWhitespace() { while (pos < text.length && text[pos].isWhitespace()) pos++ }
        fun readQuotedString(): String? {
            skipWhitespace()
            if (pos >= text.length || text[pos] != '"') return null
            pos++
            val sb = StringBuilder()
            while (pos < text.length) {
                val c = text[pos]
                if (c == '"') { pos++; break }
                if (c == '\\' && pos + 1 < text.length) { pos++; sb.append(text[pos]); pos++; continue }
                sb.append(c); pos++
            }
            return sb.toString()
        }
        fun parseNode(): KVNode? {
            skipWhitespace()
            val name = readQuotedString() ?: return null
            skipWhitespace()
            if (pos >= text.length) return KVNode(name)
            if (text[pos] == '{') {
                pos++
                val node = KVNode(name, "", mutableListOf())
                while (pos < text.length) {
                    skipWhitespace()
                    if (pos >= text.length) break
                    if (text[pos] == '}') { pos++; break }
                    if (text[pos] == '"') {
                        val childValue = readQuotedString()
                        if (childValue != null) {
                            var afterStr = pos
                            skipWhitespace()
                            val savedPos = pos
                            if (pos < text.length && text[pos] == '"') {
                                val sval = readQuotedString()
                                node.children.add(KVNode(childValue, sval ?: ""))
                            } else if (pos < text.length && text[pos] == '{') {
                                val inner = KVNode(childValue, "", mutableListOf())
                                pos = savedPos
                                val childNode = parseNode()
                                if (childNode != null) node.children.add(childNode)
                            } else {
                                node.children.add(KVNode(childValue))
                                pos = savedPos
                            }
                        }
                    } else break
                }
                return node
            }
            val savedPos = pos
            val valStr = readQuotedString()
            if (valStr != null) return KVNode(name, valStr)
            pos = savedPos
            return KVNode(name)
        }
        skipWhitespace()
        if (pos >= text.length || text[pos] != '"') return null
        return parseNode()
    }

    suspend fun downloadManifestFromCDN(depotId: Int, manifestId: Long, requestCode: Long, cdnHost: String = "liveru.steamcontent.com"): ByteArray? {
        while (true) {
            yield()
            try {
                val rcUnsigned = requestCode.toULong()
                val path = if (rcUnsigned > 0U) {
                    "depot/$depotId/manifest/$manifestId/5/$rcUnsigned"
                } else {
                    "depot/$depotId/manifest/$manifestId/5"
                }
                val url = URL("https://$cdnHost/$path")
                val conn = url.openConnection() as HttpURLConnection
                conn.setRequestProperty("User-Agent", "Valve/Steam HTTP Client 1.0")
                conn.connectTimeout = 15000
                conn.readTimeout = 15000
                if (conn.responseCode != 200) { conn.disconnect(); delay(3000); continue }
                val data = conn.inputStream.readBytes()
                conn.disconnect()
                if (data.isEmpty()) { Log.w("SteamCM", "[cdn] Empty response, retrying..."); delay(3000); continue }
                val zip = java.util.zip.ZipInputStream(ByteArrayInputStream(data))
                val entry = zip.nextEntry
                if (entry == null) { zip.close(); delay(3000); continue }
                val manifestBytes = zip.readBytes()
                zip.closeEntry(); zip.close()
                if (manifestBytes.isEmpty()) { delay(3000); continue }
                return manifestBytes
            } catch (e: Exception) {
                Log.w("SteamCM", "[cdn] Manifest download failed, retrying: ${e.message}")
                delay(3000)
            }
        }
    }

    fun decryptFilename(encrypted: ByteArray, depotKey: ByteArray): String {
        val raw = String(encrypted, Charsets.UTF_8)
        val decoded: ByteArray
        try {
            val normalized = raw
                .replace('+', '-')
                .replace('/', '_')
                .replace("\n", "")
                .replace("\r", "")
                .replace(" ", "")
            decoded = Base64.decode(normalized, Base64.URL_SAFE)
        } catch (e: Exception) {
            return raw
        }
        if (decoded.size < 16) return raw
        return try {
            val ecb = Cipher.getInstance("AES/ECB/NoPadding")
            ecb.init(Cipher.DECRYPT_MODE, SecretKeySpec(depotKey, "AES"))
            val iv = ByteArray(16)
            ecb.doFinal(decoded, 0, 16, iv, 0)
            val cbc = Cipher.getInstance("AES/CBC/PKCS5Padding")
            cbc.init(Cipher.DECRYPT_MODE, SecretKeySpec(depotKey, "AES"), IvParameterSpec(iv))
            val plaintext = cbc.doFinal(decoded, 16, decoded.size - 16)
            String(plaintext, Charsets.UTF_8).trimEnd('\u0000')
        } catch (e: Exception) {
            raw
        }
    }

    fun parseManifestFiles(data: ByteArray, depotKey: ByteArray): List<ManifestFile> {
        val rdr = ProtoReader(data)
        var payloadBytes: ByteArray? = null

        while (rdr.remaining >= 4) {
            val magic = rdr.readInt32LE()
            when (magic) {
                0x71F617D0 -> {
                    val plen = rdr.readInt32LE()
                    payloadBytes = rdr.readBytes(plen)
                }
                0x1F4812BE -> {
                    val mlen = rdr.readInt32LE()
                    val meta = rdr.readBytes(mlen)
                    val mr = ProtoReader(meta)
                    while (mr.remaining > 0) {
                        val t = mr.readVarint()
                        when (t and 7) { 0 -> mr.readVarint64(); 1 -> mr.skip(8); 2 -> { val l = mr.readVarint(); mr.skip(l) }; 5 -> mr.skip(4); else -> mr.skip(1) }
                    }
                }
                0x1B81B817 -> { val slen = rdr.readInt32LE(); rdr.skip(slen) }
                0x32C415AB -> { break }
                else -> { Log.w("SteamCM", "[manifest] Unknown magic: 0x%08X".format(magic)); break }
            }
        }

        if (payloadBytes == null) return emptyList()

        val files = mutableListOf<ManifestFile>()
        val pr = ProtoReader(payloadBytes)
        while (pr.remaining > 0) {
            val tag = pr.readVarint()
            val fn = tag shr 3; val wt = tag and 0x7
            if (fn == 1 && wt == 2) {
                val len = pr.readVarint()
                val end = pr.pos + len
                var path = ""
                var size = 0L; var crc32 = 0
                val chunks = mutableListOf<ChunkInfo>()
                var rawFn: ByteArray? = null
                while (pr.pos < end) {
                    val ft = pr.readVarint()
                    val ffn = ft shr 3; val fwt = ft and 0x7
                    when {
                        ffn == 1 && fwt == 2 -> { val sl = pr.readVarint(); rawFn = pr.readBytes(sl) }
                        ffn == 2 && fwt == 0 -> size = pr.readVarint64()
                        ffn == 3 && fwt == 0 -> crc32 = pr.readVarint()
                        ffn == 6 && fwt == 2 -> {
                            val clen = pr.readVarint()
                            val cend = pr.pos + clen
                            var sha1: ByteArray? = null; var chkCrc = 0; var chkOff = 0L; var chkOrig = 0; var chkComp = 0
                            while (pr.pos < cend) {
                                val ct = pr.readVarint()
                                val cfn = ct shr 3; val cwt = ct and 0x7
                                when (cfn) {
                                    1 -> { val sl = pr.readVarint(); sha1 = pr.readBytes(sl) }
                                    2 -> chkCrc = pr.readInt32LE()
                                    3 -> chkOff = pr.readVarint64()
                                    4 -> chkOrig = pr.readVarint()
                                    5 -> chkComp = pr.readVarint()
                                    else -> when (cwt) { 0 -> pr.readVarint64(); 1 -> pr.skip(8); 2 -> { val l = pr.readVarint(); pr.skip(l) }; 5 -> pr.skip(4) }
                                }
                            }
                            if (sha1 != null) chunks.add(ChunkInfo(sha1, chkCrc, chkOff, chkComp, chkOrig))
                        }
                        else -> when (fwt) { 0 -> pr.readVarint64(); 1 -> pr.skip(8); 2 -> { val l = pr.readVarint(); pr.skip(l) }; 5 -> pr.skip(4) }
                    }
                }
                if (rawFn != null) path = decryptFilename(rawFn, depotKey)
                if (path.isNotEmpty()) files.add(ManifestFile(path.replace('\\', '/'), size, crc32, chunks))
            } else {
                when (wt) { 0 -> pr.readVarint64(); 1 -> pr.skip(8); 2 -> { val l = pr.readVarint(); pr.skip(l) }; 5 -> pr.skip(4); else -> pr.skipField() }
            }
        }
        return files
    }

    suspend fun downloadChunkAndDecrypt(depotId: Int, chunk: ChunkInfo, depotKey: ByteArray, cdnHost: String = "liveru.steamcontent.com"): ByteArray {
        val hex = chunk.sha1.joinToString("") { "%02x".format(it) }
        while (true) {
            yield()
            try {
                val url = URL("https://$cdnHost/depot/$depotId/chunk/$hex")
                val conn = url.openConnection() as HttpURLConnection
                conn.setRequestProperty("User-Agent", "Valve/Steam HTTP Client 1.0")
                conn.connectTimeout = 15000
                conn.readTimeout = 15000
                if (conn.responseCode != 200) { conn.disconnect(); delay(3000); continue }
                val encrypted = conn.inputStream.readBytes()
                conn.disconnect()
                if (encrypted.size < 16) {
                    Log.w("SteamCM", "[chunk] Too small (${encrypted.size} bytes), retrying...")
                    delay(3000); continue
                }

                val ecb = Cipher.getInstance("AES/ECB/NoPadding")
                ecb.init(Cipher.DECRYPT_MODE, SecretKeySpec(depotKey, "AES"))
                val iv = ecb.doFinal(encrypted, 0, 16)

                val cbc = Cipher.getInstance("AES/CBC/PKCS5Padding")
                cbc.init(Cipher.DECRYPT_MODE, SecretKeySpec(depotKey, "AES"), IvParameterSpec(iv))
                val decrypted = cbc.doFinal(encrypted, 16, encrypted.size - 16)

                val decompressed = when {
                    decrypted.size >= 4 && decrypted[0] == 'P'.code.toByte() && decrypted[1] == 'K'.code.toByte() && decrypted[2] == 0x03.toByte() && decrypted[3] == 0x04.toByte() -> {
                        Log.d("SteamCM", "[chunk] PKZip chunk=${hex.take(16)}")
                        val zis = ZipInputStream(ByteArrayInputStream(decrypted))
                        val entry = zis.nextEntry ?: throw Exception("Empty PKZip entry")
                        val data = zis.readBytes()
                        zis.closeEntry(); zis.close()
                        data
                    }
                    decrypted.size >= 4 && decrypted[0] == 'V'.code.toByte() && decrypted[1] == 'S'.code.toByte() && decrypted[2] == 'Z'.code.toByte() && decrypted[3] == 'a'.code.toByte() -> {
                        throw Exception("Zstd compression not supported")
                    }
                    decrypted.size >= 17 && decrypted[0] == 'V'.code.toByte() && decrypted[1] == 'Z'.code.toByte() && decrypted[2] == 'a'.code.toByte() -> {
                        Log.d("SteamCM", "[chunk] VZip/LZMA chunk=${hex.take(16)}")
                        val compressed = decrypted
                        val propBits = compressed[7]
                        val dictSize = (compressed[8].toInt() and 0xFF) or
                                ((compressed[9].toInt() and 0xFF) shl 8) or
                                ((compressed[10].toInt() and 0xFF) shl 16) or
                                ((compressed[11].toInt() and 0xFF) shl 24)
                        val footerOff = compressed.size - 10
                        val decompressedSize = (compressed[footerOff + 4].toInt() and 0xFF) or
                                ((compressed[footerOff + 5].toInt() and 0xFF) shl 8) or
                                ((compressed[footerOff + 6].toInt() and 0xFF) shl 16) or
                                ((compressed[footerOff + 7].toInt() and 0xFF) shl 24)
                        Log.d("SteamCM", "[chunk] VZip props=0x%02x dictSize=%d uncompSize=%d totalSize=%d".format(propBits.toInt() and 0xFF, dictSize, decompressedSize, compressed.size))
                        val vzipCrc = (compressed[footerOff].toInt() and 0xFF) or
                                ((compressed[footerOff + 1].toInt() and 0xFF) shl 8) or
                                ((compressed[footerOff + 2].toInt() and 0xFF) shl 16) or
                                ((compressed[footerOff + 3].toInt() and 0xFF) shl 24)
                        val windowBuf = ByteArray(maxOf(1 shl 12, dictSize))
                        val dest = ByteArray(decompressedSize)
                        val bytesRead = LZMAInputStream(ByteArrayInputStream(compressed, 12, compressed.size - 12), decompressedSize.toLong(), propBits, dictSize, windowBuf).use { lzma ->
                            var n = 0
                            while (n < decompressedSize) {
                                val count = lzma.read(dest, n, decompressedSize - n)
                                if (count < 0) break
                                n += count
                            }
                            n
                        }
                        if (bytesRead != decompressedSize) {
                            throw Exception("VZip decompressed size mismatch: got $bytesRead expected $decompressedSize")
                        }
                        val actualVzipCrc = java.util.zip.CRC32().apply { update(dest) }.value.toInt()
                        Log.d("SteamCM", "[chunk] VZip CRC: expected=0x%08x got=0x%08x match=%s".format(vzipCrc, actualVzipCrc, if (vzipCrc == actualVzipCrc) "yes" else "no"))
                        Log.d("SteamCM", "[chunk] VZip first32: %s".format(dest.take(32).joinToString("") { "%02x".format(it) }))
                        dest
                    }
                    else -> {
                        throw Exception("Unknown compression: ${decrypted.take(4).joinToString("") { "%02x".format(it) }}")
                    }
                }

                val chunkAdler = adler32(decompressed)
                if (chunkAdler != chunk.checksum) {
                    Log.e("SteamCM", "[chunk] Checksum mismatch, retrying...")
                    delay(3000); continue
                }

                return decompressed
            } catch (e: Exception) {
                Log.w("SteamCM", "[chunk] Retrying after failure: ${e.message}")
                delay(3000)
            }
        }
    }

    suspend fun assembleFileFromChunks(depotId: Int, file: ManifestFile, out: File, depotKey: ByteArray, cdnHost: String = "liveru.steamcontent.com"): Boolean {
        return try {
            if (file.chunks.isEmpty()) {
                if (out.name.contains('.')) {
                    out.createNewFile()
                } else {
                    out.mkdirs()
                }
                return true
            }
            var p = out.parentFile
            while (p != null) {
                if (p.isFile) {
                    Log.w("SteamCM", "[file] ENOTDIR fix: deleting '${p.path}' which conflicts with directory")
                    p.delete()
                }
                p = p.parentFile
            }
            out.parentFile?.mkdirs()
            val tmp = File(out.parentFile, "${out.name}.tmp")
            RandomAccessFile(tmp, "rw").use { raf ->
                raf.setLength(file.size)
                coroutineScope {
                    file.chunks.map { chunk ->
                        async<Unit>(Dispatchers.IO) {
                            val data = downloadChunkAndDecrypt(depotId, chunk, depotKey, cdnHost)
                            synchronized(raf) {
                                raf.seek(chunk.offset)
                                raf.write(data)
                            }
                        }
                    }.awaitAll()
                }
            }
            if (out.exists()) out.delete()
            tmp.renameTo(out)
            true
        } catch (e: Exception) {
            val tmp = File(out.parentFile, "${out.name}.tmp")
            if (tmp.exists()) {
                Log.d("SteamCM", "[file] Cleaning up tmp: ${tmp.path}")
                tmp.delete()
            }
            Log.e("SteamCM", "[file] Failed: ${file.path}: ${e.message}")
            false
        }
    }
}

// Steam uses Adler32 with initial seed 0 (rfc1950 specifies seed 1)
private fun adler32(data: ByteArray): Int {
    var a = 0; var b = 0
    for (byte in data) {
        a = (a + (byte.toInt() and 0xFF)) % 65521
        b = (b + a) % 65521
    }
    return a or (b shl 16)
}
