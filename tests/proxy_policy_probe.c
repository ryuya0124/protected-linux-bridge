/* MIT. Policy-only regression tests. Does not start the proxy or a VM. */
#include "../native/network_proxy.c"
static int failures,checks;
#define CHECK(test) do{checks++;if(!(test)){fprintf(stderr,"Policy failed at line %d\n",__LINE__);failures++;}}while(0)
int main(void){
 char host[256],port[6];
 CHECK(!authority("deb.debian.org",host,sizeof(host),port,80)&&!strcmp(host,"deb.debian.org")&&!strcmp(port,"80"));
 CHECK(!authority("deb.debian.org:443",host,sizeof(host),port,0)&&!strcmp(port,"443"));
 CHECK(!authority("[2001:db8::1]:443",host,sizeof(host),port,0)&&!strcmp(host,"2001:db8::1"));
 const char *bad[]={"","user@host:443","host:0","host:65536","host:-1","host:","host/path","host?query","host#fragment","host\\path","host\r\n:443","[::1]trailing","host:nope"};
 for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++)CHECK(authority(bad[i],host,sizeof(host),port,0)<0);
 const char *v4[]={"0.0.0.0","127.0.0.1","127.50.0.1","169.254.1.2","224.0.0.1","255.255.255.255"};
 for(size_t i=0;i<sizeof(v4)/sizeof(*v4);i++){struct sockaddr_in a={.sin_family=AF_INET};inet_pton(AF_INET,v4[i],&a.sin_addr);CHECK(forbidden_address((struct sockaddr*)&a));}
 const char *v6[]={"::","::1","fe80::1","ff02::1","::ffff:127.0.0.1"};
 for(size_t i=0;i<sizeof(v6)/sizeof(*v6);i++){struct sockaddr_in6 a={.sin6_family=AF_INET6};inet_pton(AF_INET6,v6[i],&a.sin6_addr);CHECK(forbidden_address((struct sockaddr*)&a));}
 const unsigned char *names[]={(const unsigned char*)"example.com",(const unsigned char*)"1.1.1.1",(const unsigned char*)"2001:4860:4860::8888"};
 for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)CHECK(plb_valid_host(names[i],strlen((const char*)names[i])));
 const char *bad_hosts[]={"", "host/path", "host%eth0", "host\n", "user@host", "[::1]"};
 for(size_t i=0;i<sizeof(bad_hosts)/sizeof(*bad_hosts);i++)CHECK(!plb_valid_host((const unsigned char*)bad_hosts[i],strlen(bad_hosts[i])));
 const unsigned char embedded[]={ 'a',0,'b' };CHECK(!plb_valid_host(embedded,sizeof(embedded)));
 unsigned char echo[1401]={8,0};uint16_t sum=plb_icmp_checksum(echo,64);echo[2]=(unsigned char)(sum>>8);echo[3]=(unsigned char)sum;
 CHECK(plb_echo_request_valid(echo,64));echo[60]=1;CHECK(!plb_echo_request_valid(echo,64));echo[60]=0;
 CHECK(!plb_echo_request_valid(echo,7));CHECK(!plb_echo_request_valid(echo,sizeof(echo)));
 echo[0]=0;CHECK(!plb_echo_request_valid(echo,64));echo[0]=8;echo[1]=1;CHECK(!plb_echo_request_valid(echo,64));
 printf("Policy checks: %d, failures: %d\n",checks,failures);return !!failures;
}
