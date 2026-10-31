/* Bounded float-query reply: magic, pname, count, sixteen IEEE754 words.
 * Appended only to a packet whose LAST record is glGetFloatv (147).
 * Ordinary frame responses remain byte-for-byte compatible. */
static unsigned p2d_query_count(unsigned pname){switch(pname){
 case 0xba6:case 0xba7:case 0xba8:return 16;
 case 0xb00:case 0xba2:return 4;case 0xb70:return 2;
 case 0xd33:case 0xd31:case 0x84e2:return 1;default:return 0;
}}
