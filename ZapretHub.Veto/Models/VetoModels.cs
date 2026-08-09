namespace ZapretHub.Veto.Models
{
    public class VetoSettings
    {
        public bool Enabled { get; set; } = false;
        public bool Autostart { get; set; } = false;
        public string LuaScript { get; set; } = "";
        public string AttackMode { get; set; } = "fake";
        public string HostlistPath { get; set; } = "";
        public int MaxStreams { get; set; } = 4096;
        public int TimeoutMs { get; set; } = 30000;
    }

    public class VetoStatus
    {
        public bool IsRunning { get; set; }
        public string Version { get; set; } = "1.0.0";
        public string LuaScript { get; set; } = "";
        public ulong PacketsProcessed { get; set; }
        public ulong PacketsModified { get; set; }
        public ulong PacketsFaked { get; set; }
        public ulong PacketsDropped { get; set; }
    }
}
