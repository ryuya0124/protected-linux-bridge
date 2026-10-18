/* MIT. Guest PulseAudio PCM pipes, no host audio device passthrough. */
#ifndef PLB_GUEST_AUDIO_H
#define PLB_GUEST_AUDIO_H
static void start_audio(void){
 if(access("/usr/bin/pulseaudio",X_OK))return;
 mkdir("/run/plb/audio",0700);
 const char config[]="load-module module-native-protocol-unix socket=/run/plb/audio/native auth-anonymous=1\nload-module module-pipe-sink sink_name=android file=/run/plb/audio/playback format=s16le rate=48000 channels=2\nload-module module-pipe-source source_name=android_microphone file=/run/plb/audio/capture format=s16le rate=48000 channels=1\nset-default-sink android\nset-default-source android_microphone\n";
 if(guest_file("/run/plb/audio/pulse.pa",config)||guest_file("/run/plb/audio/asound.conf","pcm.!default { type pulse server \"unix:/run/plb/audio/native\" }\nctl.!default { type pulse server \"unix:/run/plb/audio/native\" }\n"))return;
 pid_t child=fork();if(child==0){configure_environment();setenv("DBUS_SESSION_BUS_ADDRESS","unix:path=/run/plb/no-session-bus",1);int log=open("/tmp/plb-pulse.log",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(log>=0){dup2(log,1);dup2(log,2);close(log);}execl("/usr/bin/pulseaudio","pulseaudio","--daemonize=no","--use-pid-file=no","--exit-idle-time=-1","--disable-shm=yes","--enable-memfd=no","-nF","/run/plb/audio/pulse.pa",(char*)NULL);_exit(127);}
}
static void audio_session(int client,int capture){
 const char *path=capture?"/run/plb/audio/capture":"/run/plb/audio/playback";int fd=-1;
 for(int i=0;i<160;i++){fd=open(path,(capture?O_WRONLY:O_RDONLY)|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(fd>=0)break;usleep(50000);}
 struct stat st;unsigned char status=fd<0?2:fstat(fd,&st)||!S_ISFIFO(st.st_mode)?3:0;plb_write_all(client,&status,1);
 if(!status){if(!capture){unsigned char stale[4096];for(int i=0;i<16&&read(fd,stale,sizeof(stale))>0;i++){};}plb_relay(client,fd,guest_lifetime,1);}if(fd>=0)close(fd);
}
#endif
