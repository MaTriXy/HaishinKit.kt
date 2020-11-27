package com.haishinkit.flv.tag

import com.haishinkit.flv.FlvAudioCodec
import com.haishinkit.flv.FlvSoundRate
import com.haishinkit.flv.FlvSoundSize
import com.haishinkit.flv.FlvSoundType
import java.nio.ByteBuffer

data class FlvAudioTag(
    override val type: Int = FlvTag.TYPE_AUDIO,
    override var dataSize: Long,
    override var timestamp: Long,
    override var timestampExtended: Int,
    override var streamId: Long,
    override var offset: Long,
    override var payload: ByteBuffer?,
    var codec: Byte = FlvAudioCodec.UNKNOWN,
    var soundRate: Byte = FlvSoundRate.kHz5_5,
    var soundSize: Byte = FlvSoundSize.SOUND_8BIT,
    var soundType: Byte = FlvSoundType.MONO
) : FlvTag {
    override fun toByteArray(): ByteArray {
        return byteArrayOf()
    }
}
