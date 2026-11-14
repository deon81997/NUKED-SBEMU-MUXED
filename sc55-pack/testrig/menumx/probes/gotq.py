from PIL import Image
def got_quit(v, tmp):
    """GOT: Esc until its 'Quit to DOS?' box (the green border goes dark), then Enter"""
    for _ in range(8):
        v.key('esc', 2000); v.shot(tmp)
        p = Image.open(tmp).convert('RGB').getpixel((10, 10))
        if p[0] < 20 and 60 < p[1] < 100 and p[2] < 20: v.key('ret', 4000); return True
    return False
