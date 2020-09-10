import matplotlib
import matplotlib.pyplot as plt
import numpy as np


#public, public_std = (303187.77, 323907.92), (1956.91, 27188.35)
#hidden, hidden_std = (51876, 34476), (1232.92, 2291.85)
public, public_std = (303.18777, 323.90792), (1.95691, 27.18835)
hidden, hidden_std = (51.876, 34.476), (1.23292, 2.29185)

width = 0.25
r1 = np.arange(len(public))  # the x locations for the groups
r2 = [x + width for x in r1]
r3 = [x - (width/2) for x in r2]

fig, ax = plt.subplots()
rects1 = ax.bar(r1, public, width, yerr=public_std,
                label='Public Volume', align='center')
rects2 = ax.bar(r2, hidden, width, yerr=hidden_std,
                label='Artifice', align='center')
#rects2 = ax.bar(r3, usb, width, yerr=usb_std,
#                label='USB Flash Drive')

# Add some text for labels, title and custom x-axis tick labels, etc.
ax.set_ylabel('Throughput (MB/s)')
ax.set_title('Artifice Performance')
ax.set_xticks(r3)
ax.set_xticklabels(('Read', 'Write'))
ax.legend()


def autolabel(rects, xpos='center'):
    """
    Attach a text label above each bar in *rects*, displaying its height.

    *xpos* indicates which side to place the text w.r.t. the center of
    the bar. It can be one of the following {'center', 'right', 'left'}.
    """

    ha = {'center': 'center', 'right': 'left', 'left': 'right'}
    offset = {'center': 0, 'right': 1, 'left': -1}

    for rect in rects:
        height = rect.get_height()
        ax.annotate('{}'.format(height),
                    xy=(rect.get_x() + rect.get_width() / 2, height),
                    xytext=(offset[xpos]*3, 3),  # use 3 points offset
                    textcoords="offset points",  # in both directions
                    ha=ha[xpos], va='bottom')


#autolabel(rects1, "left")
#autolabel(rects2, "right")

fig.tight_layout()

plt.show()
