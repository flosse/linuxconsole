        //  Cyborg 3D Joystick Digital mode



#include <pc.h>
#include <stdio.h>
#include <malloc.h>
#include <time.h>


int buttons[14]={
        1,4,8,2,
        0xe,0xa,6,0xc,
        9,5,
        0xf,0xb,7,3
};

int blookup[16];


#define JOYPORT 0x201

#define DELAY   10 //50

#define REPEAT 1024

unsigned long start;

void delay1()           //delay for 1 usec
{
        for(int j=0;j<71;j++);
}

void delay(unsigned long usec)
{

        for(int i=0;i<usec;i++) delay1();
}


int getjoystat(int *x,int *y,int *r,int *t)     //returns buttons
{
        delay(1500);    //reset stick


        *x=0;*y=0;*r=0;*t=0;

        int b=0;

        int q;

        outportb(JOYPORT,0);

        while( ((q=inportb(JOYPORT))&0xf) !=0) {        //get the axes
                if (q&8) (*t)++;
                if (q&4) (*r)++;
                if (q&2) (*y)++;
                if (q&1) (*x)++;
        }

        if ( (q&0xf0)!=0xf0) {          //is a button pressed ?

                q>>=4;          
                q^=0xf;

                b=1<<blookup[q];        //button pressed

                while(1) {              //check if other buttons pressed
                
                        delay(310);

                        outportb(JOYPORT,0);
        
                        delay(70);
        
                        q=inportb(JOYPORT);

                        q>>=4;
                        q^=0xf;

                        if (q==0) return b;

                        b|=1<<blookup[q];
                }

        }

        return b;               //Button not pressed
}

char * buts(int b)
{
        static char but[15];
        but[14]=0;

        for(int i=0;i<14;i++) {
                if (b&1) but[i]='X'; else but[i]='.';
                b>>=1;
        }

        return but;
}

int main()
{

        for(int i=0;i<14;i++) blookup[buttons[i]]=i;

        unsigned long ax=0,ay=0,ar=0,at=0;


        while(1)
        {                

                int x,y,r,t,b;
                b=getjoystat(&x,&y,&r,&t);

                                        //take out a bit of the flicker

                ax=ax*249+x*256*7;
                ax=(ax+0x80)>>8;

                ay=ay*249+y*256*7;
                ay=(ay+0x80)>>8;

                ar=ar*249+r*256*7;
                ar=(ar+0x80)>>8;

                at=at*249+t*256*7;
                at=(at+0x80)>>8;


                x=(ax+0x80)>>8;
                y=(ay+0x80)>>8;
                r=(ar+0x80)>>8;
                t=(at+0x80)>>8;
                        
                printf("X: %4d  Y: %4d  R: %4d  T: %4d B: %s \n",x,y,r,t,buts(b));
        }

}


